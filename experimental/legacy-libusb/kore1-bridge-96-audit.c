/* Native Instruments KORE 1 -> virtual CoreMIDI source (macOS).
 * Build: cc -std=c11 -Wall -Wextra -O2 -o kore-midi-v6 kore-midi-v6.c $(pkg-config --cflags --libs libusb-1.0) -framework CoreMIDI -framework CoreFoundation
 * v0.4 adds pooled asynchronous iso OUT and packet-size histograms.
 * v0.3.1 retries CAIAQ device-info requests after arming bulk IN.
 * v0.3 adds opt-in synchronized CAIAQ silent isochronous OUT, experimental.
 * v0.2 adds opt-in CAIAQ AUDIO_PARAMS handshake followed by iso IN diagnostics. Audio playback NOT supported.
 * v10.1 adds relative jog wheel CC28 output. Audio interface NOT supported.
 * v10 adds opt-in bidirectional Mixxx feedback (MIDI CC+SysEx) and live LCD.
 * v9 adds an explicitly requested experimental KORE 1 LCD framebuffer test.
 * v8 adds an explicitly requested KORE 1 LED self-test using the caiaq 0x0c protocol.
 * v7 adds button and joystick/analog discovery logging while v6 preserves relative motion by splitting deltas into <=63-unit MIDI events,
 * adds optional ERP4 spike guard and heartbeat diagnostics. Experimental; LED/LCD commands are intentionally not sent until packet format is verified.
 */
#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>
#include <libusb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <stdint.h>
#include <pthread.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define VID 0x17cc
#define PID 0x4711
#define EP_IN 0x81
#define EP_OUT 0x01
#define BUFSIZE 512
static volatile sig_atomic_t running=1;
static uint64_t clock_ms(void);
static int audio_startup_trace=0;
static uint64_t audio_trace_origin=0;
#define TRACE_STARTUP(...) do { if(audio_startup_trace) { printf("STARTUP t=%llums ", (unsigned long long)(clock_ms()-audio_trace_origin)); printf(__VA_ARGS__); printf("\n"); fflush(stdout); } } while(0)


/* v0.13 opt-in fixed-length OUT diagnostic; v0.6 sync statistics preserved. */
/* v0.15 headphone-focused guarded tone probe: diagnostic, not a verified DAC mapping. */
static int hp_tone=0;
static int pcm_fifo_mode=0;
static int pcm_fd=-1;
static unsigned char pcm_cache[8192];
static size_t pcm_cache_at=0, pcm_cache_len=0;
static uint64_t pcm_underflows=0, pcm_frames_read=0;
static unsigned char pcm_stereo_bytes[6];
static int pcm_stereo_valid=0;
static int hp_pattern=0;
static uint64_t hp_audio_start_ms=0;
static int hp_last_phase=-1;
static uint64_t hp_packet_hashes[3]={0,0,0};
static uint64_t hp_packet_counts[3]={0,0,0};


static double hp_phase=0.0;
static uint64_t hp_start_ms=0;
static uint64_t hp_tone_samples=0;
static int hp_last_active=-1;
#define AUDIO_EP_IN 0x82
#define AUDIO_EP_OUT 0x06
#define AUDIO_PACKETS 8
#define AUDIO_PACKET_SIZE 512
static int audio_probe=0;
static int audio_init=0, audio_duplex=0;
static int audio_echo_short=0; /* experimental: replay latest 2-byte ISO IN frame */
static unsigned char audio_latest_short[2]={0,0};
static uint64_t audio_echo_updates=0, audio_echo_sends=0;
static uint64_t audio_first_out_ms=0, audio_last_out_ms=0, audio_first_in_ms=0, audio_last_in_ms=0;
static int audio_independent_short=0; /* v0.23: independent 2-byte packets */
static int audio_tx_independent=0; /* experimental fixed OUT loop, independent of RX callbacks */
static int audio_start_prime=0; /* opt-in: submit one fixed-size silent OUT batch before IN */
static int audio_linux_urbs=0; /* opt-in Linux-style 32 concurrent IN URBs */
static int audio_checkbyte_pattern=0; /* 0=linux,1=zero,2=phase-invert,3=swap-stream,4=constant 01 03,5=constant 00 02 */
static int audio_checkbyte_zero=0; /* opt-in A/B control: zero the Linux alignment-2 check bytes */
static uint64_t audio_checkbytes_written=0;
static unsigned audio_payload_logs=0;
static int audio_payload_tone_logged=0;
static int audio_out_fixed=0; /* explicitly enabled; never default */
static int audio_out_96=0; /* experimental 96-byte ISO OUT, AUDIO_PARAMS stays at 176 */
static int audio_bpp=0;
static unsigned audio_tx_packet_bytes(void) { return audio_independent_short ? 2u : (audio_out_96 ? 96u : (unsigned)audio_bpp); }
static int audio_spec_seen=0, audio_alignment=-1, audio_streams=-1;
static int audio_ack=-1, audio_ack_seen=0;
static unsigned int audio_report_ms=1000;
#define AUDIO_IN_POOL 32
struct audio_in_slot {struct libusb_transfer *transfer; unsigned char *buffer; int active;};
static struct audio_in_slot audio_in_slots[AUDIO_IN_POOL];
static int audio_in_count=1;
static uint64_t audio_pcm_eligible=0,audio_pcm_short=0;
static int audio_submitted=0, audio_done=0;
static uint64_t audio_packets=0,audio_bytes=0,audio_errors=0,audio_nonzero=0;
static uint64_t audio_last_report_ms=0;
static int audio_first_printed=0;
#define AUDIO_OUT_POOL 32
struct audio_out_slot {
    struct libusb_transfer *transfer;
    unsigned char *buffer;
    int busy;
};
static struct audio_out_slot audio_out_slots[AUDIO_OUT_POOL];
static unsigned int audio_out_next=0;
static uint64_t audio_tx_packets=0, audio_tx_bytes=0, audio_tx_errors=0, audio_tx_skipped=0;
static uint64_t audio_tx_batches=0, audio_tx_first=0;
static uint64_t audio_in_hist[4]={0},audio_out_hist[4]={0};
static unsigned int audio_in_max=0,audio_out_max=0;
static int audio_hex_probe=0;
static unsigned audio_hex_in_count=0, audio_hex_out_count=0;
/* v0.6: bounded, passive statistics; no changes to the transmitted audio bytes. */
static int audio_sync_diag=0;
static uint64_t audio_sync_samples=0, audio_sync_second_nonzero=0;
static uint64_t audio_sync_frequency[256]={0}, audio_sync_delta[256]={0};
static uint64_t audio_sync_equal=0, audio_sync_changes=0;
static unsigned char audio_sync_prev=0;
static int audio_sync_have_prev=0;
static uint64_t audio_sync_two_byte=0, audio_sync_larger=0;
/* v0.13 passive inspection of the short IN frames. No assumed PCM decoding. */
static int audio_linux_trace=0; /* passive Linux startup and packet framing audit */
static uint64_t audio_status_candidate[4]={0};
static uint64_t audio_status_other=0;
static int audio_short_diag=0;
static uint64_t audio_short_pairs[4]={0}; /* 00, 01, 10, 11 for bit7/bit6 */
static uint64_t audio_short_low6[64]={0};
static uint64_t audio_short_nonzero_second=0;
static uint64_t audio_short_total=0;
static unsigned audio_short_printed=0;
static void audio_short_observe(const unsigned char *p, unsigned n) {
    if(!audio_short_diag || !p || n!=2) return;
    audio_short_total++;
    /* Linux alignment-2 check byte low six bits can only be 0,1,2,3
       for two streams; a 2-byte IN frame is not itself a full PCM frame. */
    if((p[0]&0x3f)<4) audio_status_candidate[p[0]&3]++;
    else audio_status_other++;
    audio_short_pairs[(p[0]>>6)&3]++;
    audio_short_low6[p[0]&63]++;
    if(p[1]) audio_short_nonzero_second++;
    if(audio_short_printed<12) {
        printf("AUDIO SHORT sample #%u: %02X %02X (hi2=%u low6=%u)\n",
            ++audio_short_printed,p[0],p[1],(unsigned)(p[0]>>6),(unsigned)(p[0]&63));
    }
}
static void audio_short_report(void) {
    if(!audio_short_diag) return;
    unsigned distinct=0;
    for(unsigned i=0;i<64;i++) if(audio_short_low6[i]) distinct++;
    printf("AUDIO SHORT diagnostics: two_byte=%llu second_nonzero=%llu high2=[%llu,%llu,%llu,%llu] distinct_low6=%u\n",
        (unsigned long long)audio_short_total,(unsigned long long)audio_short_nonzero_second,
        (unsigned long long)audio_short_pairs[0],(unsigned long long)audio_short_pairs[1],
        (unsigned long long)audio_short_pairs[2],(unsigned long long)audio_short_pairs[3],distinct);
    if(audio_linux_trace) printf("AUDIO LINUX AUDIT: short IN first-byte low6 in {0,1,2,3}=[%llu,%llu,%llu,%llu], other=%llu; NOT a validated status decoder\n",
        (unsigned long long)audio_status_candidate[0],(unsigned long long)audio_status_candidate[1],
        (unsigned long long)audio_status_candidate[2],(unsigned long long)audio_status_candidate[3],
        (unsigned long long)audio_status_other);
}
static uint64_t audio_out_second_nonzero=0, audio_out_first_nonzero=0;
static void audio_sync_in_packet(const unsigned char *p, unsigned n) {
    if(!audio_sync_diag || !p || n==0) return;
    if(n==2) audio_sync_two_byte++; else if(n>2) audio_sync_larger++;
    if(n<2) return;
    unsigned char x=p[0];
    audio_sync_frequency[x]++;
    if(p[1]!=0) audio_sync_second_nonzero++;
    if(audio_sync_have_prev) {
        audio_sync_delta[(unsigned char)(x-audio_sync_prev)]++;
        if(x==audio_sync_prev) audio_sync_equal++; else audio_sync_changes++;
    }
    audio_sync_prev=x; audio_sync_have_prev=1; audio_sync_samples++;
}
static void audio_sync_out_packet(const unsigned char *p, unsigned n) {
    if(!audio_sync_diag || !p || n<2) return;
    if(p[0]) audio_out_first_nonzero++;
    if(p[1]) audio_out_second_nonzero++;
}
static void audio_sync_report(void) {
    if(!audio_sync_diag) return;
    unsigned unique=0; unsigned top[8]={0};
    for(unsigned i=0;i<256;i++) {
        if(audio_sync_frequency[i]) unique++;
        for(unsigned j=0;j<8;j++) if(audio_sync_frequency[i]>audio_sync_frequency[top[j]] ||
            (audio_sync_frequency[i]==audio_sync_frequency[top[j]] && i<top[j])) {
            for(unsigned k=7;k>j;k--) top[k]=top[k-1];
            top[j]=i; break;
        }
    }
    printf("AUDIO SYNC IN samples=%llu 2byte=%llu larger=%llu second_nonzero=%llu unique_first=%u repeats=%llu changes=%llu\n",
        (unsigned long long)audio_sync_samples,(unsigned long long)audio_sync_two_byte,
        (unsigned long long)audio_sync_larger,(unsigned long long)audio_sync_second_nonzero,unique,
        (unsigned long long)audio_sync_equal,(unsigned long long)audio_sync_changes);
    printf("AUDIO SYNC top first bytes:");
    for(unsigned i=0;i<8;i++) if(audio_sync_frequency[top[i]])
        printf(" %02X:%llu",top[i],(unsigned long long)audio_sync_frequency[top[i]]);
    printf("; deltas 00:%llu 01:%llu FF:%llu; OUT nonzero first=%llu second=%llu\n",
        (unsigned long long)audio_sync_delta[0],(unsigned long long)audio_sync_delta[1],
        (unsigned long long)audio_sync_delta[255],(unsigned long long)audio_out_first_nonzero,
        (unsigned long long)audio_out_second_nonzero);
}

#define AUDIO_HEX_LIMIT 24
static void audio_hex_line(const char *dir, unsigned index, const unsigned char *data, unsigned len) {
    printf("AUDIO HEX %s #%u len=%u:",dir,index,len);
    for(unsigned j=0;j<len && j<32;j++) printf(" %02X",data[j]);
    if(len>32) printf(" ...");
    putchar('\n');
}

static int audio_hist_bucket(unsigned n) {return n==0?0:n==2?1:n<=16?2:3;}
/* Linux snd-usb-caiaq audio.c: alignment 2, silent PCM with CAIAQ check bytes. */
/* Linux audio.c fill_out_urb_mode_0, data_alignment=2:
 * two streams, four USB bytes per sample; after 2*streams PCM bytes,
 * insert one check byte per stream. Silence PCM remains zero.
 * Diagnostic A/B control can intentionally zero check bytes. */
static void v21_fill_short(unsigned char *buf,unsigned len) {
    memset(buf,0,len);
    if(audio_echo_short && len==2) {buf[0]=audio_latest_short[0];buf[1]=audio_latest_short[1];audio_echo_sends++;}
}
static void caiaq_fill_silence(unsigned char *buf, unsigned len) {
    memset(buf,0,len);
    if(audio_alignment==2 && audio_streams>0) {
        unsigned stride=(unsigned)audio_streams*4u;
        for(unsigned base=0;base<len;base+=stride) {
            unsigned check_start=base+(unsigned)audio_streams*2u;
            for(int stream=0;stream<audio_streams;stream++) {
                unsigned i=check_start+(unsigned)stream;
                if(i>=len) break;
                unsigned char check=(unsigned char)((stream<<1) | (~(i/stride)&1u));
                switch(audio_checkbyte_pattern) {
                    case 1: check=0; break;
                    case 2: check^=1u; break;
                    case 3: check=(unsigned char)((((1-stream)<<1) | (~(i/stride)&1u))); break;
                    case 4: check=(unsigned char)(stream==0?1:3); break;
                    case 5: check=(unsigned char)(stream==0?0:2); break;
                    default: break;
                }
                buf[i]=check;
                audio_checkbytes_written++;
            }
        }
    } else if(audio_alignment==3) {
        for(unsigned i=0;i+7<len;i+=8) buf[i+3]=0,buf[i+7]=1;
    }
}

/* PCM playback: faithful Linux snd-usb-caiaq alignment-2 framing.
 * Linux uses SNDRV_PCM_FMTBIT_S24_3BE and for alignment 2 loops one
 * PCM byte per stream, inserting n_streams checkbytes after
 * 2*n_streams PCM bytes. A stream has two interleaved channels, each
 * 3-byte big-endian signed sample. Other output channels remain zero.
 * This does NOT resolve firmware readiness or hardware routing. */
static uint64_t linux_pcm_frames=0;
static unsigned linux_pcm_sample_byte[2]={0,0};
static unsigned char linux_pcm_sample[2][2][3]={{{0}}};
static double linux_pcm_phase=0;
/* Read 48kHz stereo float32 LE from a nonblocking FIFO.
 * AudioQueue input is on another process; do not block USB event loop.
 * A missing frame becomes silence. No interpolation or sample rate conversion. */
static void pcm_next_frame(void) {
    float values[2]={0,0};
    if(pcm_fd>=0) {
        unsigned char raw[8]; size_t got=0;
        while(got<8) {
            if(pcm_cache_at==pcm_cache_len) {
                ssize_t n=read(pcm_fd,pcm_cache,sizeof(pcm_cache));
                if(n<=0) break;
                pcm_cache_at=0; pcm_cache_len=(size_t)n;
            }
            size_t avail=pcm_cache_len-pcm_cache_at;
            size_t n=8-got; if(n>avail)n=avail;
            memcpy(raw+got,pcm_cache+pcm_cache_at,n);
            pcm_cache_at+=n; got+=n;
        }
        if(got==8) {memcpy(values,raw,8);pcm_frames_read++;}
        else pcm_underflows++;
    } else pcm_underflows++;
    for(int c=0;c<2;c++) {
        float v=values[c]; if(!isfinite(v))v=0;
        if(v>0.25f)v=0.25f; if(v< -0.25f)v= -0.25f;
        int32_t sample=(int32_t)(v*8388607.0f);
        pcm_stereo_bytes[c*3]=(unsigned char)((sample>>16)&255);
        pcm_stereo_bytes[c*3+1]=(unsigned char)((sample>>8)&255);
        pcm_stereo_bytes[c*3+2]=(unsigned char)(sample&255);
    }
    pcm_stereo_valid=1;
}
static void linux_make_pcm_sample(int stream,int chan,double frequency) {
    if(stream!=0) {memset(linux_pcm_sample[stream][chan],0,3);return;}
    if(pcm_fifo_mode) {
        if(chan==0 || !pcm_stereo_valid) pcm_next_frame();
        memcpy(linux_pcm_sample[stream][chan],pcm_stereo_bytes+chan*3,3);
        if(chan==1) {pcm_stereo_valid=0;linux_pcm_frames++;}
        return;
    }
    /* -60 dBFS nominal, temporary diagnostic tone only */
    int32_t sample=(int32_t)(sin(linux_pcm_phase)*8192.0);
    linux_pcm_sample[stream][chan][0]=(unsigned char)((sample>>16)&255);
    linux_pcm_sample[stream][chan][1]=(unsigned char)((sample>>8)&255);
    linux_pcm_sample[stream][chan][2]=(unsigned char)(sample&255);
    if(chan==1) {
        linux_pcm_phase+=6.2831853071795864769*frequency/48000.0;
        if(linux_pcm_phase>=6.2831853071795864769) linux_pcm_phase-=6.2831853071795864769;
        linux_pcm_frames++;
    }
}
static void linux_fill_pcm(unsigned char *buf, unsigned len, int tone) {
    memset(buf,0,len);
    if(audio_alignment!=2 || audio_streams!=2) return;
    unsigned i=0;
    while(i<len) {
        for(int stream=0;stream<2 && i<len;stream++) {
            unsigned pos=linux_pcm_sample_byte[stream];
            unsigned chan=(pos/3u)&1u;
            unsigned byte=pos%3u;
            if(byte==0) linux_make_pcm_sample(stream,(int)chan,440.0);
            buf[i++]=(tone || pcm_fifo_mode)?linux_pcm_sample[stream][chan][byte]:0;
            linux_pcm_sample_byte[stream]=(pos+1u)%6u;
        }
        if(i%(2u*4u)==2u*2u) {
            for(int stream=0;stream<2 && i<len;stream++,i++)
                buf[i]=(unsigned char)((stream<<1)|(~(i/(2u*4u))&1u));
            audio_checkbytes_written+=2;
        }
    }
}
static void audio_payload_log(const unsigned char *buf,unsigned len,int tone) {
    if(!audio_out_96 || !hp_tone) return;
    if(audio_payload_logs>=2 && (!tone || audio_payload_tone_logged)) return;
    if(tone) audio_payload_tone_logged=1;
    else audio_payload_logs++;
    unsigned nonzero_pcm=0,checks=0;
    for(unsigned i=0;i<len;i++) {
        if(i%8u==4u || i%8u==5u) checks++;
        else if(buf[i]) nonzero_pcm++;
    }
    printf("AUDIO PAYLOAD %s len=%u check_positions=%u nonzero_pcm_bytes=%u first32=",
           tone?"TONE":"SILENCE",len,checks,nonzero_pcm);
    for(unsigned i=0;i<len && i<32;i++) printf(" %02X",buf[i]);
    putchar('\n'); fflush(stdout);
}
static void hp_fill_packet(unsigned char *buf, unsigned len) {
    uint64_t elapsed=clock_ms()-(hp_audio_start_ms?hp_audio_start_ms:hp_start_ms);
    int tone=hp_tone && elapsed>=1000 && elapsed<2200;
    linux_fill_pcm(buf,len,tone);
    audio_payload_log(buf,len,tone);
    if(tone) hp_packet_counts[1]++;else hp_packet_counts[0]++;
}
static uint64_t audio_tx_rearms=0, audio_tx_rearm_errors=0;
static void LIBUSB_CALL on_audio_tx(struct libusb_transfer *t) {
    struct audio_out_slot *slot=(struct audio_out_slot *)t->user_data;
    if(t->status==LIBUSB_TRANSFER_COMPLETED) {
        for(int i=0;i<t->num_iso_packets;i++) {
            const struct libusb_iso_packet_descriptor *pkt=&t->iso_packet_desc[i];
            if(pkt->status==LIBUSB_TRANSFER_COMPLETED) {
                audio_tx_packets++;
                if(!audio_first_out_ms) audio_first_out_ms=clock_ms();
                audio_last_out_ms=clock_ms();
                audio_tx_bytes+=pkt->actual_length;
                audio_out_hist[audio_hist_bucket(pkt->actual_length)]++;
                if(pkt->actual_length>audio_out_max) audio_out_max=pkt->actual_length;
            } else audio_tx_errors++;
        }
    } else if(t->status!=LIBUSB_TRANSFER_CANCELLED) {
        audio_tx_errors++;
        fprintf(stderr,"AUDIO OUT transfer status=%d\n",t->status);
    }
    if(slot) slot->busy=0;
    /* Independent transmit clock: re-submit only after the same slot completes.
     * No allocation, no duplicate submission, and never reuse an in-flight buffer. */
    if(audio_tx_independent && running && slot && t->status==LIBUSB_TRANSFER_COMPLETED) {
        if(audio_independent_short) {
            for(int k=0;k<t->num_iso_packets;k++)
                v21_fill_short(slot->buffer+k*2u,2);
        }
        if(hp_tone || pcm_fifo_mode) {
            for(int k=0;k<t->num_iso_packets;k++)
                hp_fill_packet(slot->buffer+k*audio_tx_packet_bytes(),audio_tx_packet_bytes());
        }
        slot->busy=1;
        int r=libusb_submit_transfer(t);
        if(r==0) audio_tx_rearms++;
        else {slot->busy=0;audio_tx_rearm_errors++;audio_tx_errors++;
              fprintf(stderr,"AUDIO independent OUT rearm: %s\n",libusb_error_name(r));}
    }
}
static void audio_send_silence_matching_rx(struct libusb_transfer *rx) {
    if(!audio_duplex || audio_tx_independent) return;
    struct audio_out_slot *slot=NULL;
    for(int k=0;k<AUDIO_OUT_POOL;k++) {
        unsigned i=(audio_out_next+(unsigned)k)%AUDIO_OUT_POOL;
        if(audio_out_slots[i].transfer && !audio_out_slots[i].busy) {
            slot=&audio_out_slots[i];
            audio_out_next=(i+1)%AUDIO_OUT_POOL;
            break;
        }
    }
    if(!slot) {audio_tx_skipped++;return;}
    unsigned offset=0;
    int any=0;
    for(int i=0;i<AUDIO_PACKETS;i++) {
        unsigned n=audio_out_fixed ? (unsigned)audio_bpp :
            (rx->iso_packet_desc[i].status==LIBUSB_TRANSFER_COMPLETED ? rx->iso_packet_desc[i].actual_length : 0);
        if(n>AUDIO_PACKET_SIZE) n=0;
        slot->transfer->iso_packet_desc[i].length=n;
        if(n) {
            if(n==2) v21_fill_short(slot->buffer+offset,n);
            else linux_fill_pcm(slot->buffer+offset,n,0);
            audio_sync_out_packet(slot->buffer+offset,n);
            if(audio_hex_probe && audio_hex_out_count<AUDIO_HEX_LIMIT)
                audio_hex_line("OUT",++audio_hex_out_count,slot->buffer+offset,n);
            any=1;
        }
        offset+=n;
    }
    if(!any) return;
    /* Diagnostic guard: reject incomplete RX-derived ISO batches rather than
     * passing zero-length packet descriptors to the macOS libusb backend.
     * Fixed-length mode remains unchanged. */
    if(!audio_out_fixed) {
        for(int i=0;i<AUDIO_PACKETS;i++) {
            if(slot->transfer->iso_packet_desc[i].length==0) {
                audio_tx_skipped++;
                return;
            }
        }
    }
    slot->transfer->length=(int)offset;
    slot->busy=1;
    int r=libusb_submit_transfer(slot->transfer);
    if(r) {
        slot->busy=0;
        audio_tx_errors++;
        fprintf(stderr,"AUDIO OUT submit: %s\n",libusb_error_name(r));
    } else {
        audio_tx_batches++;
        if(!audio_tx_first) {printf("AUDIO first silent OUT submitted (%s, packet bytes=%d)\n",
                audio_out_fixed?"fixed OUT lengths":"matched IN lengths",
                audio_out_fixed?audio_bpp:2);audio_tx_first=1;}
    }
}
/* v0.13 experimental stream priming: submit one 8-packet OUT batch before
 * arming any iso IN URB. This tests startup order, NOT verified Linux parity.
 * The same silence framing as v0.7 is used, without inventing protocol bytes. */
static int audio_prime_out_before_in(void) {
    struct audio_out_slot *slot=&audio_out_slots[0];
    if(!slot->transfer || !slot->buffer || slot->busy) return -1;
    if(audio_bpp!=176 || audio_alignment!=2 || audio_streams!=2) return -1;
    unsigned offset=0;
    for(int i=0;i<AUDIO_PACKETS;i++) {
        slot->transfer->iso_packet_desc[i].length=(unsigned)audio_bpp;
        linux_fill_pcm(slot->buffer+offset,(unsigned)audio_bpp,0);
        audio_sync_out_packet(slot->buffer+offset,(unsigned)audio_bpp);
        if(audio_hex_probe && audio_hex_out_count<AUDIO_HEX_LIMIT)
            audio_hex_line("OUT",++audio_hex_out_count,slot->buffer+offset,(unsigned)audio_bpp);
        offset+=(unsigned)audio_bpp;
    }
    slot->transfer->length=(int)offset;
    slot->busy=1;
    int r=libusb_submit_transfer(slot->transfer);
    if(r) {slot->busy=0;fprintf(stderr,"AUDIO v0.15 priming submit: %s\n",libusb_error_name(r));return -1;}
    audio_tx_batches++;
    puts("AUDIO v0.15 PRIME: one 8 x 176-byte OUT transfer submitted before IN; experimental order probe.");
    return 0;
}
/* v0.13: launch a bounded pool of fixed 176-byte OUT URBs without relying
 * on incoming 2-byte IN packets. OUT pacing follows USB completion events.
 * The CAIAQ payload format is still unverified; this is a diagnostic only. */
static int audio_start_independent_out(void) {
    const int slots=8;
    if(audio_bpp!=176 || (!audio_out_fixed && !audio_independent_short)) return -1;
    const unsigned tx_n=audio_tx_packet_bytes();
    for(int j=0;j<slots;j++) {
        struct audio_out_slot *slot=&audio_out_slots[j];
        if(!slot->transfer || !slot->buffer || slot->busy) return -1;
        unsigned off=0;
        for(int i=0;i<AUDIO_PACKETS;i++) {
            slot->transfer->iso_packet_desc[i].length=tx_n;
            if(tx_n==2) v21_fill_short(slot->buffer+off,tx_n);
            else hp_fill_packet(slot->buffer+off,tx_n);
            if(audio_linux_trace && j==0 && i==0) {
                printf("AUDIO LINUX TRACE: first independent OUT payload bytes:");
                for(unsigned k=0;k<tx_n && k<24;k++) printf(" %02X",slot->buffer[off+k]);
                putchar('\n');
            }
            audio_sync_out_packet(slot->buffer+off,tx_n);
            off+=tx_n;
        }
        slot->transfer->length=(int)off;
        slot->busy=1;
        int r=libusb_submit_transfer(slot->transfer);
        if(r) {slot->busy=0;fprintf(stderr,"AUDIO independent OUT start #%d: %s\n",j,libusb_error_name(r));return -1;}
        audio_tx_batches++;
    }
    printf("AUDIO v0.23 independent OUT: %d in-flight 8x%u-byte transfers, completion-driven rearm.\n",slots,tx_n);
    return 0;
}
static int audio_prepare_out(libusb_device_handle *handle) {
    if(audio_alignment!=2 || audio_streams!=2) {
        fprintf(stderr,"AUDIO duplex safety stop: expected alignment=2, streams=2.\n");return -1;
    }
    for(int i=0;i<AUDIO_OUT_POOL;i++) {
        struct audio_out_slot *slot=&audio_out_slots[i];
        slot->transfer=libusb_alloc_transfer(AUDIO_PACKETS);
        slot->buffer=calloc(AUDIO_PACKETS,AUDIO_PACKET_SIZE);
        if(!slot->transfer || !slot->buffer) {
            fprintf(stderr,"AUDIO OUT pool allocation failed at slot %d\n",i);return -1;
        }
        libusb_fill_iso_transfer(slot->transfer,handle,AUDIO_EP_OUT,slot->buffer,
            AUDIO_PACKETS*AUDIO_PACKET_SIZE,AUDIO_PACKETS,on_audio_tx,slot,0);
    }
    printf("AUDIO duplex enabled: %d-slot asynchronous silent OUT pool on EP 0x06; mode=%s.\n",
           AUDIO_OUT_POOL,audio_out_fixed?(audio_out_96?"FIXED 96-byte experiment":"FIXED 176-byte diagnostic"):"legacy matched IN lengths");
    return 0;
}

static void LIBUSB_CALL on_audio_rx(struct libusb_transfer *t) {
    if(t->status==LIBUSB_TRANSFER_COMPLETED) {
        for(int i=0;i<t->num_iso_packets;i++) {
            struct libusb_iso_packet_descriptor *p=&t->iso_packet_desc[i];
            if(p->status!=LIBUSB_TRANSFER_COMPLETED) {audio_errors++;continue;}
            if(p->actual_length >= (unsigned)audio_bpp) audio_pcm_eligible++;
            else audio_pcm_short++;
            audio_packets++;
            if(!audio_first_in_ms) audio_first_in_ms=clock_ms();
            audio_last_in_ms=clock_ms();
            audio_bytes+=(uint64_t)p->actual_length;
            audio_in_hist[audio_hist_bucket(p->actual_length)]++;
            if(p->actual_length>audio_in_max) audio_in_max=p->actual_length;
            if(p->actual_length>0) {
                audio_nonzero++;
                if(p->actual_length==2) {
                    unsigned char *q=libusb_get_iso_packet_buffer_simple(t,(unsigned)i);
                    if(q) {audio_latest_short[0]=q[0];audio_latest_short[1]=q[1];audio_echo_updates++;}
                }
                if(audio_short_diag && p->actual_length==2) {
                    unsigned char *short_pkt=libusb_get_iso_packet_buffer_simple(t,(unsigned)i);
                    audio_short_observe(short_pkt,p->actual_length);
                }
                if(audio_sync_diag) {
                    unsigned char *pkt=libusb_get_iso_packet_buffer_simple(t,(unsigned)i);
                    audio_sync_in_packet(pkt,p->actual_length);
                }
                if(audio_hex_probe && audio_hex_in_count<AUDIO_HEX_LIMIT) {
                    unsigned char *pkt=libusb_get_iso_packet_buffer_simple(t,(unsigned)i);
                    if(pkt) audio_hex_line("IN",++audio_hex_in_count,pkt,p->actual_length);
                }
                if(!audio_first_printed) {
                    printf("AUDIO first nonempty iso packet: length=%u (payload not recorded)\n",p->actual_length);
                    audio_first_printed=1;
                }
            }
        }
        if(running) {
            audio_send_silence_matching_rx(t);
            int r=libusb_submit_transfer(t);
            if(r==0) return;
            fprintf(stderr,"AUDIO rearm: %s\n",libusb_error_name(r));
            running=0;
        }
    } else if(t->status==LIBUSB_TRANSFER_CANCELLED) {
        /* Expected on shutdown. */
    } else {
        fprintf(stderr,"AUDIO transfer status=%d (probe stopping)\n",t->status);
        audio_errors++;
        running=0;
    }
    struct audio_in_slot *slot=(struct audio_in_slot *)t->user_data;
    if(slot) slot->active=0;
    audio_done=1;
    for(int j=0;j<audio_in_count;j++) if(audio_in_slots[j].active) audio_done=0;
}
static int start_audio_probe(libusb_device_handle *handle) {
    audio_in_count=audio_linux_urbs?AUDIO_IN_POOL:1;
    audio_done=0;
    for(int i=0;i<audio_in_count;i++) {
        struct audio_in_slot *slot=&audio_in_slots[i];
        slot->transfer=libusb_alloc_transfer(AUDIO_PACKETS);
        slot->buffer=malloc(AUDIO_PACKETS*AUDIO_PACKET_SIZE);
        if(!slot->transfer || !slot->buffer) {fprintf(stderr,"AUDIO IN allocation failed at %d\n",i);return -1;}
        libusb_fill_iso_transfer(slot->transfer,handle,AUDIO_EP_IN,slot->buffer,
                                 AUDIO_PACKETS*AUDIO_PACKET_SIZE,AUDIO_PACKETS,
                                 on_audio_rx,slot,0);
        libusb_set_iso_packet_lengths(slot->transfer,AUDIO_PACKET_SIZE);
        int r=libusb_submit_transfer(slot->transfer);
        if(r) {fprintf(stderr,"AUDIO IN submit #%d failed: %s\n",i,libusb_error_name(r));return -1;}
        slot->active=1;
        audio_submitted=1;
    }
    audio_last_report_ms=clock_ms();
    printf("AUDIO v0.15 IN URBs=%d, 8 x 512 bytes each, duplex OUT=%s; Linux PCM threshold=%d bytes.\n",
           audio_in_count,audio_duplex?"enabled":"disabled",audio_bpp);
    return 0;
}
static void audio_report(int final) {
    if(!audio_probe) return;
    uint64_t now=clock_ms();
    if(!final && now-audio_last_report_ms<audio_report_ms) return;
    printf("AUDIO %s packets=%llu nonempty=%llu bytes=%llu errors=%llu\n",
           final?"final":"stats",(unsigned long long)audio_packets,
           (unsigned long long)audio_nonzero,(unsigned long long)audio_bytes,
           (unsigned long long)audio_errors);
    if(audio_duplex) printf("AUDIO OUT packets=%llu bytes=%llu errors=%llu skipped_batches=%llu\n",
        (unsigned long long)audio_tx_packets,(unsigned long long)audio_tx_bytes,
        (unsigned long long)audio_tx_errors,(unsigned long long)audio_tx_skipped);
    printf("AUDIO sizes IN [0,2,3-16,>16]=[%llu,%llu,%llu,%llu] max=%u; OUT=[%llu,%llu,%llu,%llu] max=%u batches=%llu\n",
        (unsigned long long)audio_in_hist[0],(unsigned long long)audio_in_hist[1],
        (unsigned long long)audio_in_hist[2],(unsigned long long)audio_in_hist[3],audio_in_max,
        (unsigned long long)audio_out_hist[0],(unsigned long long)audio_out_hist[1],
        (unsigned long long)audio_out_hist[2],(unsigned long long)audio_out_hist[3],audio_out_max,
        (unsigned long long)audio_tx_batches);
    printf("AUDIO Linux PCM threshold: eligible=%llu short=%llu (bpp=%d; short packets are ignored by Linux PCM decoder)\n",
           (unsigned long long)audio_pcm_eligible,(unsigned long long)audio_pcm_short,audio_bpp);
    audio_sync_report();
    audio_short_report();
    fflush(stdout);
    audio_last_report_ms=now;
}

static MIDIClientRef client=0;
static MIDIEndpointRef source=0, feedback_destination=0;
static int mixxx_feedback=0;
static pthread_mutex_t feedback_lock=PTHREAD_MUTEX_INITIALIZER;
#define FEEDBACK_QUEUE_SIZE 128
struct FeedbackMessage {unsigned int length; unsigned char bytes[128];};
static struct FeedbackMessage feedback_queue[FEEDBACK_QUEUE_SIZE];
static unsigned int feedback_write=0, feedback_read=0;
static unsigned char feedback_leds[32]={0};
static int feedback_led_dirty=0, feedback_lcd_dirty=0;
static unsigned int feedback_deck=1;
static char feedback_title[2][64]={{0}}, feedback_artist[2][64]={{0}};
static int feedback_play[2]={0}, feedback_bpm10[2]={0};
static uint64_t feedback_last_lcd_ms=0,feedback_last_led_ms=0;
static int verbose=0, poll_ms=0, threshold=4;
static int raw_erp=0, raw_packets=0, no_midi=0;
static int adaptive=1, adaptive_ms=90, adaptive_tolerance=5;
static int erp4_spike_guard=0, erp4_spike_min=80, erp4_spike_ms=90;
static int erp4_spike_pending=0;
static uint64_t erp4_spike_since=0;
static uint64_t start_ms=0;
static unsigned long midi_erp_messages[8]={0};
static FILE *filter_csv=NULL;
static FILE *csv=NULL;
static FILE *controls_csv=NULL;
static int controls_raw=0, analog_cc_base=40, analog_min_change=8, io_invert=0;
static uint64_t erp_packet_count=0;
static int erp_last[8], analog_last[3];
static int diagnose_reversals=0, reversal_window_ms=180, reversal_tolerance=5;
static int pending_delta[8]={0};
static uint64_t pending_since[8]={0};
static unsigned long suppressed_pairs[8]={0};
static int have_erp=0, have_io=0, have_analog=0;
static int adapt_pending[8]={0}, adapt_dir[8]={0};
static uint64_t adapt_since[8]={0}, adapt_active_until[8]={0};
static unsigned long adapt_suppressed[8]={0};
static unsigned int io_last=0;
static int io_misc_last=-1;
static unsigned char rx[BUFSIZE];
static libusb_context *usb_ctx=NULL;
static libusb_device_handle *device=NULL;
static struct libusb_transfer *rx_transfer=NULL;
static int transfer_done=0, submitted=0;
static const int pairs[8][2]={{7,5},{12,14},{15,13},{0,2},{3,1},{8,10},{11,9},{4,6}};

static void on_signal(int sig) {(void)sig; running=0;}
static void midi3(unsigned char status,unsigned char data1,unsigned char data2) {
    if(no_midi || !source) return;
    struct { MIDIPacketList list; } packet_storage;
    MIDIPacketList *list=&packet_storage.list;
    MIDIPacket *packet=MIDIPacketListInit(list);
    unsigned char data[3]={status,data1,data2};
    packet=MIDIPacketListAdd(list,sizeof(packet_storage),packet,0,3,data);
    if(!packet || MIDIReceived(source,list)!=noErr)
        fprintf(stderr,"CoreMIDI event failed\n");
    if(verbose) printf("MIDI %02X %02X %02X\n",status,data1,data2);
}

/* CoreMIDI output callback may run on a different thread. NEVER call libusb here.
 * Queue complete messages and consume in the main USB event loop instead.
 */
static void feedback_enqueue(const unsigned char *data,size_t length) {
    if(length==0 || length>128) return;
    pthread_mutex_lock(&feedback_lock);
    unsigned int next=(feedback_write+1)%FEEDBACK_QUEUE_SIZE;
    if(next!=feedback_read) {
        struct FeedbackMessage *msg=&feedback_queue[feedback_write];
        msg->length=(unsigned int)length;
        memcpy(msg->bytes,data,length);
        feedback_write=next;
    }
    pthread_mutex_unlock(&feedback_lock);
}
static void feedback_midi_callback(const MIDIPacketList *list,void *readProcRefCon,void *srcConnRefCon) {
    (void)readProcRefCon;(void)srcConnRefCon;
    const MIDIPacket *pkt=&list->packet[0];
    for(UInt32 i=0;i<list->numPackets;i++) {
        /* SysEx is sent as one <=128-byte packet by this mapping. */
        if(pkt->length && pkt->data[0]==0xf0) feedback_enqueue(pkt->data,pkt->length);
        else for(UInt16 p=0;p+2<pkt->length;p+=3) feedback_enqueue(pkt->data+p,3);
        pkt=MIDIPacketNext(pkt);
    }
}

static int decode_erp(unsigned char a,unsigned char b) {
    const int hi=268,lo=-7,range=hi-lo,deg90=range/2,deg180=range,deg270=deg90+deg180,deg360=deg180*2,mid=(hi+lo)/2;
    int wb=abs(mid-(int)a)-(range/2-100)/2;
    if(wb<0) wb=0;
    if(wb>100) wb=100;
    int wa=100-wb;
    int pb=a<mid ? (int)b-lo+deg270 : hi-(int)b+deg90;
    if(pb>=deg360) pb-=deg360;
    int pa=b>mid ? (int)a-lo : hi-(int)a+deg180;
    int value=(pa*wa+pb*wb)*10/deg360;
    if(value<0) value+=1000;
    if(value>=1000) value-=1000;
    return value;
}
static uint64_t clock_ms(void) {
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (uint64_t)tv.tv_sec*1000u+(uint64_t)tv.tv_usec/1000u;
}
static void emit_erp(int i,int delta) {
    if(!delta) return;
    int amount=abs(delta);
    while(amount>0) {
        int step=amount>63 ? 63 : amount;
        midi3(0xB0,(unsigned char)(20+i),(unsigned char)(delta>0 ? step : 64+step));
        midi_erp_messages[i]++;
        amount-=step;
    }
}
static void flush_pending(uint64_t now,int force) {
    for(int i=0;i<8;i++) {
        if(pending_delta[i] && (force || now-pending_since[i]>(uint64_t)reversal_window_ms)) {
            if(verbose) printf("ERP %d confirmed delayed delta %+d\n",i+1,pending_delta[i]);
            emit_erp(i,pending_delta[i]);
            pending_delta[i]=0;
        }
    }
}
static void handle_erp_delta(int i,int delta,uint64_t now) {
    if(!diagnose_reversals) {emit_erp(i,delta);return;}
    int old=pending_delta[i];
    if(old && now-pending_since[i]<=(uint64_t)reversal_window_ms &&
       (old>0)!=(delta>0) && abs(old+delta)<=reversal_tolerance) {
        suppressed_pairs[i]++;
        printf("ERP %d REVERSAL candidate: %+d then %+d, net %+d, suppressed (count %lu)\n",
               i+1,old,delta,old+delta,suppressed_pairs[i]);
        pending_delta[i]=0;
        return;
    }
    if(old) {
        emit_erp(i,old);
        pending_delta[i]=0;
    }
    pending_delta[i]=delta;
    pending_since[i]=now;
}
static void filter_event(uint64_t now,int i,const char *event,int delta,int emitted) {
    if(filter_csv) fprintf(filter_csv,"%llu,%d,%s,%d,%d,%lu\n",
        (unsigned long long)now,i+1,event,delta,emitted,adapt_suppressed[i]);
    if(verbose && (!strcmp(event,"suppressed") || !strcmp(event,"confirmed") || !strcmp(event,"timeout")))
        printf("FILTER ERP %d %s delta=%+d emitted=%+d\n",i+1,event,delta,emitted);
}
/* Two consistent updates confirm a moving encoder immediately. An isolated
 * update waits briefly; a near-cancelling reversal is suppressed. Once
 * confirmed, subsequent same-direction updates are emitted without waiting.
 * Fast intentional reversals can still be suppressed if they cancel quickly.
 */
static void adaptive_delta(int i,int delta,uint64_t now) {
    if(!delta) return;
    int old=adapt_pending[i];
    if(old) {
        if((old>0)!=(delta>0) && abs(old+delta)<=adaptive_tolerance &&
           now-adapt_since[i]<=(uint64_t)adaptive_ms) {
            adapt_pending[i]=0;
            adapt_dir[i]=0;
            adapt_active_until[i]=0;
            adapt_suppressed[i]++;
            filter_event(now,i,"suppressed",delta,0);
            return;
        }
        if((old>0)==(delta>0)) {
            emit_erp(i,old);
            emit_erp(i,delta);
            filter_event(now,i,"confirmed",old+delta,old+delta);
            adapt_pending[i]=0;
            adapt_dir[i]=(delta>0)?1:-1;
            adapt_active_until[i]=now+(uint64_t)adaptive_ms;
            return;
        }
        emit_erp(i,old);
        filter_event(now,i,"timeout_or_reversal",old,old);
        adapt_pending[i]=0;
    }
    int dir=delta>0?1:-1;
    if(adapt_dir[i]==dir && now<=adapt_active_until[i]) {
        emit_erp(i,delta);
        adapt_active_until[i]=now+(uint64_t)adaptive_ms;
        filter_event(now,i,"active",delta,delta);
    } else {
        adapt_pending[i]=delta;
        adapt_since[i]=now;
        adapt_dir[i]=0;
        filter_event(now,i,"pending",delta,0);
    }
}
/* Optional ERP4 large-jump check: suppress a near-cancelling return, otherwise
 * release the held delta. Disabled by default to preserve fast movement. */
static void guarded_adaptive_delta(int i,int delta,uint64_t now) {
    if(i!=3 || !erp4_spike_guard) {adaptive_delta(i,delta,now);return;}
    if(erp4_spike_pending) {
        int old=erp4_spike_pending;
        if(now-erp4_spike_since<=(uint64_t)erp4_spike_ms &&
           (old>0)!=(delta>0) && abs(old+delta)<=adaptive_tolerance) {
            erp4_spike_pending=0;
            adapt_suppressed[i]++;
            filter_event(now,i,"spike_suppressed",old+delta,0);
            return;
        }
        erp4_spike_pending=0;
        adaptive_delta(i,old,now);
    }
    if(abs(delta)>=erp4_spike_min) {
        erp4_spike_pending=delta;
        erp4_spike_since=now;
        filter_event(now,i,"spike_pending",delta,0);
    } else adaptive_delta(i,delta,now);
}
static void flush_erp4_spike(uint64_t now,int force) {
    if(erp4_spike_pending && (force || now-erp4_spike_since>(uint64_t)erp4_spike_ms)) {
        int d=erp4_spike_pending;
        erp4_spike_pending=0;
        adaptive_delta(3,d,now);
    }
}
static void flush_adaptive(uint64_t now,int force) {
    for(int i=0;i<8;i++) {
        if(adapt_pending[i] && (force || now-adapt_since[i]>(uint64_t)adaptive_ms)) {
            int d=adapt_pending[i];
            adapt_pending[i]=0;
            emit_erp(i,d);
            filter_event(now,i,"timeout",d,d);
        }
    }
}
static void process(const unsigned char *d,int len) {
    if(len<1) return;
    const unsigned char *p=d+1;
    int n=len-1;
    if(d[0]==0x01 && n>=14) {
        int n_in=p[8]>p[10]?p[8]:p[10];
        int n_out=p[7]>p[9]?p[7]:p[9];
        audio_streams=(n_in>n_out?n_in:n_out)/2;
        audio_alignment=p[13];
        audio_spec_seen=1;
        if(audio_init) printf("AUDIO device spec: analog in=%u out=%u, digital in=%u out=%u, alignment=%d, paired-streams=%d\n",
            p[8],p[7],p[10],p[9],audio_alignment,audio_streams);
    }
    if(d[0]==0x09) {
        if(n>=1) {
            audio_ack=(int)p[0]; audio_ack_seen=1;
            printf("AUDIO_PARAMS firmware response: status=%d (%s)\n",audio_ack,
                audio_ack==1?"accepted": "not accepted / unknown status");
            fflush(stdout);
        } else fprintf(stderr,"AUDIO_PARAMS response missing status byte\n");
        return;
    }
    if(d[0]==0x02 && n>=16) {
        uint64_t timestamp=clock_ms();
        ++erp_packet_count;
        if(raw_packets) {
            printf("USB ERP packet=%llu timestamp_ms=%llu len=%d bytes=",(unsigned long long)erp_packet_count,(unsigned long long)timestamp,len);
            for(int j=0;j<len;j++) printf("%02X",d[j]);
            putchar('\n');
        }
        for(int i=0;i<8;i++) {
            unsigned char a=p[pairs[i][0]], b=p[pairs[i][1]];
            int v=decode_erp(a,b);
            int delta=have_erp ? v-erp_last[i] : 0;
            if(delta>500) delta-=1000;
            if(delta< -500) delta+=1000;
            int candidate=have_erp && abs(delta)>=threshold;
            if((raw_erp || csv) && (!have_erp || delta!=0)) {
                if(raw_erp) printf("RAW ERP %d pkt=%llu t=%llu bytes[%d]=%02X bytes[%d]=%02X value=%d prev=%d delta=%+d candidate=%d\n",
                    i+1,(unsigned long long)erp_packet_count,(unsigned long long)timestamp,pairs[i][0],a,pairs[i][1],b,v,have_erp?erp_last[i]:-1,delta,candidate);
                if(csv) fprintf(csv,"%llu,%llu,%d,%d,%u,%d,%u,%d,%d,%d,%d\n",
                    (unsigned long long)timestamp,(unsigned long long)erp_packet_count,i+1,pairs[i][0],(unsigned)a,pairs[i][1],(unsigned)b,v,have_erp?erp_last[i]:-1,delta,candidate);
            }
            if(have_erp) {
                if(candidate) {
                    if(adaptive) guarded_adaptive_delta(i,delta,timestamp);
                    else handle_erp_delta(i,delta,timestamp);
                    if(verbose) printf("ERP %d: %d -> %d (delta %+d)\n",i+1,erp_last[i],v,delta);
                }
            }
            erp_last[i]=v;
        }
        have_erp=1;
    } else if(d[0]==0x04 && n>=4) {
        unsigned int bits=(unsigned int)p[0] | ((unsigned int)p[1]<<8) | ((unsigned int)p[2]<<16) | ((unsigned int)p[3]<<24);
        if(have_io) {
            unsigned int changes=bits^io_last;
            for(int i=0;i<32;i++) if(changes & (1u<<i)) {
                int pressed=((bits & (1u<<i))!=0) ^ io_invert;
                midi3(pressed ? 0x90 : 0x80,(unsigned char)(36+i),pressed ? 100 : 0);
                if(verbose) printf("IO bit %d %s\n",i,pressed ? "pressed" : "released");
                if(controls_csv) fprintf(controls_csv,"%llu,button,%d,%d,%u,%u\n",(unsigned long long)clock_ms(),i,pressed,bits,(unsigned)(n>=5?p[4]:0));
            }
        }
        io_last=bits;
        have_io=1;
        if(n>=5) {
            int misc=255-p[4];
            if((verbose || controls_raw) && misc!=io_misc_last) printf("IO misc: %d (raw=%u)\n",misc,(unsigned)p[4]);
            if(controls_csv && misc!=io_misc_last) fprintf(controls_csv,"%llu,misc,0,%d,%u,0\n",(unsigned long long)clock_ms(),misc,(unsigned)p[4]);
            if(io_misc_last>=0 && misc!=io_misc_last) {
                // KORE big wheel is 8-bit circular ABS_MISC. Emit relative CC28.
                int delta=misc-io_misc_last;
                if(delta>127) delta-=256;
                if(delta< -128) delta+=256;
                // Split into MIDI-safe relative chunks (1..63 / 65..127).
                int remain=delta;
                while(remain!=0) {
                    int step=abs(remain)>63?63:abs(remain);
                    midi3(0xB0,28,(unsigned char)(remain>0?step:64+step));
                    remain+=remain>0?-step:step;
                }
            }
            io_misc_last=misc;
        }
    } else if(d[0]==0x03 && n>=6) {
        for(int i=0;i<3;i++) {
            int v=((int)p[2*i]<<8)|p[2*i+1];
            if(controls_csv && (!have_analog || v!=analog_last[i])) fprintf(controls_csv,"%llu,analog,%d,%d,%d,%d\n",(unsigned long long)clock_ms(),i,v,have_analog?analog_last[i]:-1,0);
            if((controls_raw || verbose) && (!have_analog || v!=analog_last[i])) printf("ANALOG RAW %d: %d (bytes %02X %02X)\n",i,v,(unsigned)p[2*i],(unsigned)p[2*i+1]);
            if(have_analog && abs(v-analog_last[i])>=analog_min_change) {
                int cc=(v*127+2047)/4095;
                if(cc>127) cc=127;
                midi3(0xB0,(unsigned char)(analog_cc_base+i),(unsigned char)cc);
                if(verbose) printf("ANALOG %d: %d -> %d\n",i,analog_last[i],v);
            }
            analog_last[i]=v;
        }
        have_analog=1;
    } else if(verbose) {
        printf("USB IN cmd=%02X len=%d\n",d[0],len);
    }
}
static void LIBUSB_CALL on_rx(struct libusb_transfer *t) {
    if(t->status==LIBUSB_TRANSFER_COMPLETED) {
        if(t->actual_length>0) process(t->buffer,t->actual_length);
        if(running) {
            int r=libusb_submit_transfer(t);
            if(r==0) return;
            fprintf(stderr,"USB rearm: %s\n",libusb_error_name(r));
            running=0;
        }
    } else if(t->status!=LIBUSB_TRANSFER_CANCELLED) {
        fprintf(stderr,"USB receive status %d\n",t->status);
        running=0;
    }
    transfer_done=1;
}
static int send_usb(const unsigned char *buf,int len) {
    int sent=0;
    int r=libusb_bulk_transfer(device,EP_OUT,(unsigned char *)buf,len,&sent,1000);
    if(r || sent!=len) {
        fprintf(stderr,"USB OUT failed: %s (%d/%d)\n",libusb_error_name(r),sent,len);
        return -1;
    }
    return 0;
}
/* Linux snd-usb-caiaq device.c: EP1_CMD_AUDIO_PARAMS, bank=0.
 * 48000Hz code=1, 24bit code=2, 16-bit LE bytes/microframe, packets/microframe=1.
 * bpp derived exactly as the Linux ALSA driver does for alignment 0,2,3.
 * Sending this message changes the device's audio configuration; opt-in only.
 */
static int audio_event_wait(uint64_t until_ms) {
    while(running && clock_ms()<until_ms) {
        struct timeval tv={0,20000};
        int r=libusb_handle_events_timeout(usb_ctx,&tv);
        if(r && r!=LIBUSB_ERROR_INTERRUPTED) {
            fprintf(stderr,"AUDIO waiting for firmware: %s\n",libusb_error_name(r));
            return -1;
        }
    }
    return 0;
}
static int run_audio_init(void) {
    TRACE_STARTUP("audio_init_enter spec_seen=%d", audio_spec_seen);
    /* GET_DEVICE_INFO is a one-shot response. If the initial request was
     * missed during startup, explicitly retry it while the bulk-IN transfer
     * is already armed. Never guess stream count or data alignment. */
    puts("AUDIO init: waiting for CAIAQ GET_DEVICE_INFO response...");
    for(int attempt=0; running && !audio_spec_seen && attempt<4; attempt++) {
        if(attempt>0) {
            const unsigned char info[]={0x01};
            printf("AUDIO init: retry GET_DEVICE_INFO %d/3\n",attempt);
            TRACE_STARTUP("GET_DEVICE_INFO retry=%d",attempt);
            fflush(stdout);
            if(send_usb(info,(int)sizeof(info))) return -1;
        }
        uint64_t limit=clock_ms()+(attempt==0?600:900);
        while(running && !audio_spec_seen && clock_ms()<limit) {
            if(audio_event_wait(clock_ms()+20)) return -1;
        }
    }
    TRACE_STARTUP("GET_DEVICE_INFO result seen=%d alignment=%d streams=%d",audio_spec_seen,audio_alignment,audio_streams);
    if(!audio_spec_seen) {
        fprintf(stderr,"AUDIO init aborted: GET_DEVICE_INFO unanswered after 3 retries; no audio commands sent.\n");
        return -1;
    }
    if(audio_streams<1 || audio_streams>32 ||
       (audio_alignment!=0 && audio_alignment!=2 && audio_alignment!=3)) {
        fprintf(stderr,"AUDIO init aborted: unexpected stream count=%d or alignment=%d.\n",audio_streams,audio_alignment);
        return -1;
    }
    const int sample_bytes=(audio_alignment>=2)?4:3;
    audio_bpp=((48000/8000)+5)*sample_bytes*2*audio_streams;
    if(audio_bpp>512) audio_bpp=512;
    unsigned char command[]={0x09,0x01,0x02,
        (unsigned char)(audio_bpp&255), (unsigned char)(audio_bpp>>8),0x01};
    printf("AUDIO init sending 0x09: 48000 Hz, 24 bits, alignment=%d, streams=%d, bpp=%d, microframe=1\n",
           audio_alignment,audio_streams,audio_bpp);
    audio_ack_seen=0; audio_ack=-1;
    TRACE_STARTUP("AUDIO_PARAMS command bytes=09 01 02 %02X %02X 01",command[3],command[4]);
    if(send_usb(command,(int)sizeof(command))) return -1;
    uint64_t limit=clock_ms()+1500;
    while(running && !audio_ack_seen && clock_ms()<limit) {
        if(audio_event_wait(clock_ms()+20)) return -1;
    }
    if(!audio_ack_seen) {fprintf(stderr,"AUDIO init timeout: no 0x09 firmware response.\n");return -1;}
    if(audio_ack!=1) {fprintf(stderr,"AUDIO init rejected: firmware status=%d. No iso transfer started.\n",audio_ack);return -1;}
    puts("AUDIO init ACCEPTED. Starting isochronous diagnostics.");
    if(audio_linux_trace) puts("AUDIO LINUX TRACE: AUDIO_PARAMS ACK=1; Linux audio.c stream_start submits 32 IN URBs; OUT submitted only from IN completion. No KORE-specific audio-start command in Linux device.c setup_card.");
    return 0;
}
static long elapsed_ms(struct timeval *start,struct timeval *end) {
    return (end->tv_sec-start->tv_sec)*1000L+(end->tv_usec-start->tv_usec)/1000;
}
/* LED indices are from the KORE 1/2 caiaq control table (OpenKoreBridge,
 * GPL-2.0-or-later). 0x0c carries the whole 32-byte brightness state.
 * This opt-in diagnostic replaces the entire LED state and restores all-off.
 */
static int led_test_index=-1, led_test_level=35, led_test_duration_ms=1200;
static const char *led_names[32]={
    "F3","touch7","touch3","F7","F4","touch8","touch4","F8",
    "F1","touch5","touch1","F5","F2","touch6","touch2","F6",
    "up","listen","left","record","down","play","right","stop",
    "enter","big","control","view","menu","esc","lcd","sound"
};
static int parse_led_name(const char *s) {
    for(int i=0;i<32;i++) if(!strcmp(s,led_names[i])) return i;
    return -1;
}
static int led_send_state(const unsigned char levels[32]) {
    unsigned char pkt[33]={0x0c};
    memcpy(pkt+1,levels,32);
    return send_usb(pkt,(int)sizeof(pkt));
}
static int run_led_test(void) {
    unsigned char values[32]={0};
    values[led_test_index]=(unsigned char)led_test_level;
    printf("LED test: %s, state index %d, level %d/63 for %d ms\n",
           led_names[led_test_index],led_test_index,led_test_level,led_test_duration_ms);
    if(led_send_state(values)) return -1;
    uint64_t until=clock_ms()+(uint64_t)led_test_duration_ms;
    int ok=0;
    while(running && clock_ms()<until) {
        struct timeval tv={0,20000};
        int r=libusb_handle_events_timeout(usb_ctx,&tv);
        if(r && r!=LIBUSB_ERROR_INTERRUPTED) {ok=-1; break;}
    }
    memset(values,0,sizeof(values));
    if(led_send_state(values)) ok=-1;
    puts("LED test complete (all LED levels set to zero). Note: prior LED state was not saved.");
    return ok;
}
/* LCD transfer follows OpenKoreBridge KORE 2 reference. KORE 1 is UNVALIDATED.
 * The opt-in test uses 128x64 page-oriented framebuffer and packetized writes.
 * Start with a low contrast; never write LCD packets unless --lcd-test is given.
 */
#define LCD_W 128
#define LCD_H 64
static unsigned char lcd_fb[LCD_W*LCD_H/8];
static int lcd_test=0, lcd_contrast_pct=45, lcd_backlight_pct=65;
static int lcd_invert=0, lcd_duration_ms=5000;
static int lcd_blank_on_exit=0;
static int lcd_send(const unsigned char *p,int len,const char *label) {
    if(verbose) {
        printf("LCD OUT %s:",label);
        for(int i=0;i<len && i<8;i++) printf(" %02X",p[i]);
        printf(" (len=%d)\n",len);
    }
    if(send_usb(p,len)) {fprintf(stderr,"LCD stage failed: %s\n",label);return -1;}
    /* Original KORE 2 reference delays about 12 ms between USB commands. */
    struct timeval tv={0,12000};
    int r=libusb_handle_events_timeout(usb_ctx,&tv);
    return (r && r!=LIBUSB_ERROR_INTERRUPTED) ? -1 : 0;
}
static int lcd_cmd1(unsigned char v) {
    unsigned char p[]={0x08,0x00,0x01,v};
    return lcd_send(p,sizeof(p),"cmd1");
}
static int lcd_cmd2(unsigned char a,unsigned char b) {
    unsigned char p[]={0x08,0x00,0x02,a,b};
    return lcd_send(p,sizeof(p),"cmd2");
}
static int lcd_initialize(void) {
    const unsigned char init[]={0xe2,0xa1,0xc8,0xa2,0x2c,0x2e,0x2f,0x27};
    for(size_t i=0;i<sizeof(init);i++) if(lcd_cmd1(init[i])) return -1;
    if(lcd_cmd2(0x81,(unsigned char)((lcd_contrast_pct*63+50)/100))) return -1;
    const unsigned char tail[]={0xa6,0x88,0xef,0xaf};
    for(size_t i=0;i<sizeof(tail);i++) if(lcd_cmd1(tail[i])) return -1;
    return 0;
}
/* 5x7 glyphs needed by the test message, column-major with LSB at top. */
static const unsigned char *lcd_glyph(char c) {
    static const unsigned char space[5]={0,0,0,0,0};
    static const unsigned char K[5]={0x7f,0x08,0x14,0x22,0x41};
    static const unsigned char O[5]={0x3e,0x41,0x41,0x41,0x3e};
    static const unsigned char R[5]={0x7f,0x09,0x19,0x29,0x46};
    static const unsigned char E[5]={0x7f,0x49,0x49,0x49,0x41};
    static const unsigned char one[5]={0,0x42,0x7f,0x40,0};
    static const unsigned char M[5]={0x7f,0x02,0x0c,0x02,0x7f};
    static const unsigned char I[5]={0x41,0x41,0x7f,0x41,0x41};
    static const unsigned char D[5]={0x7f,0x41,0x41,0x22,0x1c};
    static const unsigned char B[5]={0x7f,0x49,0x49,0x49,0x36};
    static const unsigned char G[5]={0x3e,0x41,0x49,0x49,0x7a};
    static const unsigned char T[5]={0x01,0x01,0x7f,0x01,0x01};
    static const unsigned char S[5]={0x46,0x49,0x49,0x49,0x31};
    static const unsigned char glyph_65[5]={0x7e,0x11,0x11,0x11,0x7e};
    static const unsigned char glyph_67[5]={0x3e,0x41,0x41,0x41,0x22};
    static const unsigned char glyph_70[5]={0x7f,0x09,0x09,0x09,0x01};
    static const unsigned char glyph_72[5]={0x7f,0x08,0x08,0x08,0x7f};
    static const unsigned char glyph_74[5]={0x20,0x40,0x41,0x3f,0x01};
    static const unsigned char glyph_76[5]={0x7f,0x40,0x40,0x40,0x40};
    static const unsigned char glyph_78[5]={0x7f,0x06,0x18,0x60,0x7f};
    static const unsigned char glyph_80[5]={0x7f,0x09,0x09,0x09,0x06};
    static const unsigned char glyph_81[5]={0x3e,0x41,0x51,0x21,0x5e};
    static const unsigned char glyph_85[5]={0x3f,0x40,0x40,0x40,0x3f};
    static const unsigned char glyph_86[5]={0x1f,0x20,0x40,0x20,0x1f};
    static const unsigned char glyph_87[5]={0x7f,0x20,0x18,0x20,0x7f};
    static const unsigned char glyph_88[5]={0x63,0x14,0x08,0x14,0x63};
    static const unsigned char glyph_89[5]={0x07,0x08,0x70,0x08,0x07};
    static const unsigned char glyph_90[5]={0x61,0x51,0x49,0x45,0x43};
    static const unsigned char glyph_48[5]={0x3e,0x51,0x49,0x45,0x3e};
    static const unsigned char glyph_50[5]={0x62,0x51,0x49,0x49,0x46};
    static const unsigned char glyph_51[5]={0x22,0x41,0x49,0x49,0x36};
    static const unsigned char glyph_52[5]={0x18,0x14,0x12,0x7f,0x10};
    static const unsigned char glyph_53[5]={0x2f,0x49,0x49,0x49,0x31};
    static const unsigned char glyph_54[5]={0x3e,0x49,0x49,0x49,0x30};
    static const unsigned char glyph_55[5]={0x01,0x71,0x09,0x05,0x03};
    static const unsigned char glyph_56[5]={0x36,0x49,0x49,0x49,0x36};
    static const unsigned char glyph_57[5]={0x06,0x49,0x49,0x29,0x1e};
    static const unsigned char glyph_46[5]={0x00,0x60,0x60,0x00,0x00};
    static const unsigned char glyph_45[5]={0x08,0x08,0x08,0x08,0x08};
    static const unsigned char glyph_58[5]={0x00,0x36,0x36,0x00,0x00};
    static const unsigned char glyph_47[5]={0x20,0x10,0x08,0x04,0x02};
    static const unsigned char glyph_43[5]={0x08,0x08,0x3e,0x08,0x08};
    switch(c) {
      case 'A':return glyph_65;
      case 'C':return glyph_67;
      case 'F':return glyph_70;
      case 'H':return glyph_72;
      case 'J':return glyph_74;
      case 'L':return glyph_76;
      case 'N':return glyph_78;
      case 'P':return glyph_80;
      case 'Q':return glyph_81;
      case 'U':return glyph_85;
      case 'V':return glyph_86;
      case 'W':return glyph_87;
      case 'X':return glyph_88;
      case 'Y':return glyph_89;
      case 'Z':return glyph_90;
      case '0':return glyph_48;
      case '2':return glyph_50;
      case '3':return glyph_51;
      case '4':return glyph_52;
      case '5':return glyph_53;
      case '6':return glyph_54;
      case '7':return glyph_55;
      case '8':return glyph_56;
      case '9':return glyph_57;
      case '.':return glyph_46;
      case '-':return glyph_45;
      case ':':return glyph_58;
      case '/':return glyph_47;
      case '+':return glyph_43;
      case 'K':return K; case 'O':return O; case 'R':return R;
      case 'E':return E; case '1':return one; case 'M':return M;
      case 'I':return I; case 'D':return D; case 'B':return B;
      case 'G':return G; case 'T':return T; case 'S':return S;
      default:return space;
    }
}
static void lcd_pixel(int x,int y) {
    if(x>=0 && x<LCD_W && y>=0 && y<LCD_H)
        lcd_fb[(y/8)*LCD_W+x] |= (unsigned char)(1u << (y%8));
}
static void lcd_text(int x,int y,const char *str,int scale) {
    for(;*str;str++,x+=scale*6) {
        const unsigned char *g=lcd_glyph(*str);
        for(int dx=0;dx<5;dx++) for(int dy=0;dy<7;dy++)
            if(g[dx] & (1u<<dy))
                for(int sx=0;sx<scale;sx++) for(int sy=0;sy<scale;sy++)
                    lcd_pixel(x+dx*scale+sx,y+dy*scale+sy);
    }
}
static void lcd_make_test_pattern(void) {
    memset(lcd_fb,0,sizeof(lcd_fb));
    for(int x=0;x<LCD_W;x++) {lcd_pixel(x,0);lcd_pixel(x,LCD_H-1);}
    for(int y=0;y<LCD_H;y++) {lcd_pixel(0,y);lcd_pixel(LCD_W-1,y);}
    lcd_text(22,9,"KORE 1",2);
    lcd_text(19,30,"MIDI BRIDGE",1);
    lcd_text(37,46,"LCD TEST",1);
}
static int lcd_send_framebuffer(void) {
    for(int page=0;page<8;page++) for(int block=0;block<4;block++) {
        unsigned char page_cmd[]={0x08,0x00,0x01,(unsigned char)(0xb0+page)};
        unsigned char addr[]={0x08,0x00,0x02,(unsigned char)(0x10+block*2),0x04};
        unsigned char data[35]={0x08,0x01,0x20};
        for(int j=0;j<32;j++) {
            unsigned char v=lcd_fb[page*LCD_W+block*32+j];
            data[3+j]=lcd_invert ? (unsigned char)~v : v;
        }
        if(lcd_send(page_cmd,sizeof(page_cmd),"page") ||
           lcd_send(addr,sizeof(addr),"address") ||
           lcd_send(data,sizeof(data),"frame data")) return -1;
    }
    return 0;
}
static int run_lcd_test(void) {
    /* Explicit opt-in; do not send unknown LCD instructions in normal mode. */
    printf("EXPERIMENTAL KORE 1 LCD test: contrast=%d%% backlight=%d%% duration=%d ms\n",
           lcd_contrast_pct,lcd_backlight_pct,lcd_duration_ms);
    unsigned char leds[32]={0};
    leds[30]=(unsigned char)((lcd_backlight_pct*127+50)/100);
    if(led_send_state(leds)) return -1;
    if(lcd_initialize()) return -1;
    lcd_make_test_pattern();
    if(lcd_send_framebuffer()) return -1;
    puts("LCD framebuffer sent. Inspect display now. Ctrl+C exits early.");
    uint64_t until=clock_ms()+(uint64_t)lcd_duration_ms;
    while(running && clock_ms()<until) {
        struct timeval tv={0,20000};
        int r=libusb_handle_events_timeout(usb_ctx,&tv);
        if(r && r!=LIBUSB_ERROR_INTERRUPTED) return -1;
    }
    if(lcd_blank_on_exit) {
        memset(lcd_fb,0,sizeof(lcd_fb));
        if(lcd_send_framebuffer()) return -1;
        memset(leds,0,sizeof(leds));
        if(led_send_state(leds)) return -1;
    }
    puts("LCD test complete. Framebuffer and backlight left unchanged by default.");
    return 0;
}

/* Feedback mini-protocol, application-local / noncommercial SysEx identifier 0x7d:
 * F0 7D 4B 01 DECK FIELD <ASCII 7-bit chars> F7
 * DECK 0/1; FIELD 1=title, 2=artist. CC channel16: CC0..31 LED 0..127,
 * CC40 deck select (0/1), CC41 play state, CC42 BPMx10 high 7 bits, CC43 BPMx10 low 7 bits.
 */
static void feedback_handle(const struct FeedbackMessage *msg) {
    const unsigned char *d=msg->bytes;
    unsigned int n=msg->length;
    if(n>=7 && d[0]==0xf0 && d[1]==0x7d && d[2]==0x4b && d[3]==0x01 && d[n-1]==0xf7) {
        unsigned int deck=d[4],field=d[5];
        if(deck<2 && (field==1 || field==2)) {
            char *target=field==1?feedback_title[deck]:feedback_artist[deck];
            unsigned int m=n-7;
            if(m>63) m=63;
            for(unsigned int i=0;i<m;i++) target[i]=(d[6+i]>=32 && d[6+i]<127)?(char)d[6+i]:' ';
            target[m]=0;
            if(deck+1==feedback_deck) feedback_lcd_dirty=1;
            if(verbose) printf("Mixxx deck %u %s: %s\n",deck+1,field==1?"title":"artist",target);
        }
    } else if(n>=3 && (d[0]&0xf0)==0xb0 && (d[0]&15)==15) {
        unsigned int cc=d[1],val=d[2];
        if(cc<32) {
            unsigned char level=(unsigned char)((val*63+63)/127);
            if(feedback_leds[cc]!=level) {feedback_leds[cc]=level;feedback_led_dirty=1;}
        } else if(cc==40) {
            if(val<2) {feedback_deck=val+1;feedback_lcd_dirty=1;}
        } else if(cc==41) {
            feedback_play[feedback_deck-1]=val?1:0;feedback_lcd_dirty=1;
        } else if(cc==42) {
            feedback_bpm10[feedback_deck-1]=(val<<7)|(feedback_bpm10[feedback_deck-1]&127);feedback_lcd_dirty=1;
        } else if(cc==43) {
            feedback_bpm10[feedback_deck-1]=(feedback_bpm10[feedback_deck-1]&(~127))|val;feedback_lcd_dirty=1;
        }
    }
}
static void feedback_render(void) {
    char line[40];
    int d=(int)feedback_deck-1;
    memset(lcd_fb,0,sizeof(lcd_fb));
    snprintf(line,sizeof(line),"DECK %c %s",d?'B':'A',feedback_play[d]?"PLAY":"STOP");
    lcd_text(2,2,line,1);
    /* 21-character fit in 128px at 6px per character */
    char title[22]={0},artist[22]={0};
    snprintf(title,sizeof(title),"%.21s",feedback_title[d][0]?feedback_title[d]:"NO TRACK");
    snprintf(artist,sizeof(artist),"%.21s",feedback_artist[d]);
    for(size_t i=0;title[i];i++) if(title[i]>='a' && title[i]<='z') title[i]-=32;
    for(size_t i=0;artist[i];i++) if(artist[i]>='a' && artist[i]<='z') artist[i]-=32;
    lcd_text(2,17,title,1);
    lcd_text(2,30,artist,1);
    snprintf(line,sizeof(line),"BPM %d.%d",feedback_bpm10[d]/10,feedback_bpm10[d]%10);
    lcd_text(2,49,line,1);
}
static int feedback_pump(void) {
    for(;;) {
        struct FeedbackMessage msg;
        int ready=0;
        pthread_mutex_lock(&feedback_lock);
        if(feedback_read!=feedback_write) {
            msg=feedback_queue[feedback_read];
            feedback_read=(feedback_read+1)%FEEDBACK_QUEUE_SIZE;
            ready=1;
        }
        pthread_mutex_unlock(&feedback_lock);
        if(!ready) break;
        feedback_handle(&msg);
    }
    uint64_t now=clock_ms();
    if(feedback_led_dirty && now-feedback_last_led_ms>=40) {
        if(led_send_state(feedback_leds)) return -1;
        feedback_last_led_ms=now;feedback_led_dirty=0;
    }
    if(feedback_lcd_dirty && now-feedback_last_lcd_ms>=250) {
        feedback_render();
        if(lcd_send_framebuffer()) return -1;
        feedback_last_lcd_ms=now;feedback_lcd_dirty=0;
    }
    return 0;
}
static int feedback_initialize(void) {
    feedback_leds[30]=(unsigned char)((lcd_backlight_pct*127+50)/100);
    if(led_send_state(feedback_leds)) return -1;
    if(lcd_initialize()) return -1;
    feedback_lcd_dirty=1;
    feedback_led_dirty=0;
    return feedback_pump();
}
static void usage(const char *name) {
    printf("Usage: %s [--erp4-spike-guard] [--erp4-spike-min N] [--erp4-spike-ms N] [--no-adaptive] [--adaptive-ms N] [--adaptive-tolerance N] [--filter-csv PATH] [--verbose] [--raw-erp] [--raw-packets] [--csv PATH] [--no-midi] [--poll-ms N] [--erp-threshold N] [--diagnose-reversals] [--reversal-ms N] [--reversal-tolerance N]\n",name);
    puts("Default: AUTO_MSG only (no polling). --poll-ms 50 enables diagnostic polling.");
    puts("--raw-erp logs every decoded ERP value change with original byte positions/values.");
    puts("--raw-packets dumps complete 0x02 USB packets as hex. --csv PATH saves raw ERP changes.");
    puts("--no-midi disables MIDI transmission for isolated diagnostic testing.");
    puts("Adaptive filter ON by default. --no-adaptive restores v4 behavior.");
    puts("--erp4-spike-guard enables experimental ERP4 bounce suppression (off by default).");
    puts("--erp4-spike-min N (default 80), --erp4-spike-ms N (default 90).");
    puts("--adaptive-ms N (default 90), --adaptive-tolerance N (default 5).");
    puts("--filter-csv PATH logs pending, confirmed, active, suppressed and timeout events.");
    puts("--diagnose-reversals delays each isolated ERP step up to 180 ms and suppresses near-cancelling reversals.");
    puts("Diagnostic filter is experimental; fast intentional back-and-forth motions may be suppressed.");
    puts("Virtual MIDI source: KORE 1 Bridge; channel 1, CC20-27 relative ERP, notes36-67 buttons, CC40-42 analog by default.");
    puts("v7 controls: --controls-csv PATH --controls-raw --analog-cc-base N --analog-min-change N --io-invert");
    puts("LED diagnostic: --led-list or --led-test NAME [--led-level 0..63] [--led-duration-ms 100..10000].");
    puts("WARNING: --led-test replaces all 32 LED brightness values, then sends all-off.");
    puts("--mixxx-feedback enables CoreMIDI destination KORE 1 Feedback, LED+LCD updates from Mixxx.");
    puts("--audio-probe (passive iso 0x82 IN monitor, no audio init); --audio-report-ms N (100..60000).");
    puts("--audio-init (opt-in: 48kHz/24bit CAIAQ AUDIO_PARAMS handshake + iso IN monitor; no audio OUT).");
    puts("--audio-duplex-silence (EXPERIMENTAL: audio init + silent CAIAQ iso OUT; unplug speakers/phones).");
    puts("--audio-linux-trace (v0.13: Linux initialization trace, first OUT payload and short-IN candidate checkbyte statistics; implies --audio-short-diag).");
    puts("--audio-short-diag (v0.13: passive analysis of two-byte IN packets, first 12 samples and counters).");
    puts("--audio-tx-independent (v0.13: 8 continuous fixed-size OUT URBs, independent of IN; requires --audio-duplex-silence --audio-out-fixed; cannot combine with --audio-start-prime). ");
    puts("--audio-start-prime (v0.13: send one 8x176-byte silent OUT transfer before arming IN; requires --audio-duplex-silence --audio-out-fixed). ");
    puts("--audio-linux-urbs (v0.13: 32 concurrent iso IN transfers, matching Linux snd-usb-caiaq; opt-in). ");
    puts("--headphone-diagnostic (v0.15: bounded output-only framing probe; NO sound or unverified PCM injection).");
    puts("--headphone-pattern (v0.23: 3s silence / 3s 440Hz / 3s silence / 3s 880Hz / 3s silence, timed from ISO OUT start). ");
    puts("--headphone-tone (v0.23: explicit experimental low-amplitude 440 Hz injection, unverified mapping).");
    puts("--audio-checkbyte-pattern linux|zero|phase|swap|fixed13|fixed02 (v0.23, requires independent fixed 176-byte OUT). ");
    puts("--audio-echo-short (v0.23: replay most recent 2-byte IN payload instead of 00 00; diagnostic only). ");
    puts("--audio-independent-short (v0.23: independent 2-byte silence OUT; requires --audio-duplex-silence; excludes --audio-out-fixed).");
    puts("--audio-out-fixed (v0.13 opt-in: send full audio_bpp bytes per OUT microframe, independent of IN lengths; requires duplex).");
    puts("--audio-out-96 (EXPERIMENTAL: independent 96-byte OUT; use with --pcm-fifo or --headphone-tone; handshake remains 176).");
    puts("Experimental LCD: --lcd-test [--lcd-contrast 0..100] [--lcd-backlight 0..100] [--lcd-duration-ms 100..30000] [--lcd-invert] [--lcd-blank-on-exit].");
    puts("WARNING: --lcd-test sends KORE 2 reference LCD USB packets to KORE 1; LCD compatibility is not verified.");
}
int main(int argc,char **argv) {
    int rc=1,claimed=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--led-list")) {
            for(int j=0;j<32;j++) printf("%2d %s\n",j,led_names[j]);
            return 0;
        }
        else if(!strcmp(argv[i],"--led-test") && i+1<argc) {
            led_test_index=parse_led_name(argv[++i]);
            if(led_test_index<0) {fprintf(stderr,"Unknown LED; run --led-list\n"); return 2;}
        }
        else if(!strcmp(argv[i],"--led-level") && i+1<argc) led_test_level=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--led-duration-ms") && i+1<argc) led_test_duration_ms=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--lcd-test")) lcd_test=1;
        else if(!strcmp(argv[i],"--mixxx-feedback")) mixxx_feedback=1;
        else if(!strcmp(argv[i],"--headphone-diagnostic")) {audio_duplex=1;audio_init=1;audio_probe=1;audio_out_fixed=1;audio_tx_independent=1;audio_linux_urbs=1;audio_linux_trace=1;audio_startup_trace=1;}
        else if(!strcmp(argv[i],"--headphone-pattern")) {hp_pattern=0;hp_tone=1;audio_duplex=1;audio_init=1;audio_probe=1;audio_out_fixed=1;audio_tx_independent=1;audio_linux_urbs=1;audio_linux_trace=1;audio_startup_trace=1;}
        else if(!strcmp(argv[i],"--pcm-fifo")) {pcm_fifo_mode=1;audio_duplex=1;audio_init=1;audio_probe=1;audio_out_fixed=1;audio_tx_independent=1;audio_linux_urbs=1;}
        else if(!strcmp(argv[i],"--headphone-tone")) {hp_tone=1;audio_duplex=1;audio_init=1;audio_probe=1;audio_out_fixed=1;audio_tx_independent=1;audio_linux_urbs=1;audio_linux_trace=1;audio_startup_trace=1;}
        else if(!strcmp(argv[i],"--audio-probe")) audio_probe=1;
        else if(!strcmp(argv[i],"--audio-init")) {audio_init=1;audio_probe=1;}
        else if(!strcmp(argv[i],"--audio-hex-probe")) {audio_hex_probe=1;audio_probe=1;}
        else if(!strcmp(argv[i],"--audio-sync-diag")) {audio_sync_diag=1;audio_probe=1;}
        else if(!strcmp(argv[i],"--audio-duplex-silence")) {audio_duplex=1;audio_init=1;audio_probe=1;}
        else if(!strcmp(argv[i],"--audio-short-diag")) audio_short_diag=1;
        else if(!strcmp(argv[i],"--audio-linux-trace")) {audio_linux_trace=1;audio_short_diag=1;}
        else if(!strcmp(argv[i],"--audio-checkbyte-zero")) {audio_checkbyte_zero=1;audio_checkbyte_pattern=1;}
        else if(!strcmp(argv[i],"--audio-checkbyte-pattern") && i+1<argc) {
            const char *s=argv[++i];
            if(!strcmp(s,"linux")) audio_checkbyte_pattern=0;
            else if(!strcmp(s,"zero")) audio_checkbyte_pattern=1;
            else if(!strcmp(s,"phase")) audio_checkbyte_pattern=2;
            else if(!strcmp(s,"swap")) audio_checkbyte_pattern=3;
            else if(!strcmp(s,"fixed13")) audio_checkbyte_pattern=4;
            else if(!strcmp(s,"fixed02")) audio_checkbyte_pattern=5;
            else {fprintf(stderr,"Unknown checkbyte pattern: %s\n",s);return 2;}
        }
        else if(!strcmp(argv[i],"--audio-tx-independent")) audio_tx_independent=1;
        else if(!strcmp(argv[i],"--audio-echo-short")) audio_echo_short=1;
        else if(!strcmp(argv[i],"--audio-independent-short")) {audio_independent_short=1;audio_tx_independent=1;}
        else if(!strcmp(argv[i],"--audio-start-prime")) audio_start_prime=1;
        else if(!strcmp(argv[i],"--audio-startup-trace")) audio_startup_trace=1;
        else if(!strcmp(argv[i],"--audio-linux-urbs")) audio_linux_urbs=1;
        else if(!strcmp(argv[i],"--audio-out-fixed")) audio_out_fixed=1;
        else if(!strcmp(argv[i],"--audio-out-96")) audio_out_96=1;
        else if(!strcmp(argv[i],"--audio-report-ms") && i+1<argc) audio_report_ms=(unsigned int)atoi(argv[++i]);
        else if(!strcmp(argv[i],"--lcd-contrast") && i+1<argc) lcd_contrast_pct=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--lcd-backlight") && i+1<argc) lcd_backlight_pct=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--lcd-duration-ms") && i+1<argc) lcd_duration_ms=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--lcd-invert")) lcd_invert=1;
        else if(!strcmp(argv[i],"--lcd-blank-on-exit")) lcd_blank_on_exit=1;
        else if(!strcmp(argv[i],"--verbose")) verbose=1;
        else if(!strcmp(argv[i],"--controls-raw")) controls_raw=1;
        else if(!strcmp(argv[i],"--io-invert")) io_invert=1;
        else if(!strcmp(argv[i],"--analog-cc-base") && i+1<argc) analog_cc_base=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--analog-min-change") && i+1<argc) analog_min_change=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--controls-csv") && i+1<argc) {
            const char *path=argv[++i]; controls_csv=fopen(path,"w");
            if(!controls_csv) {perror(path);return 2;}
            setvbuf(controls_csv,NULL,_IOLBF,0);
            fputs("timestamp_ms,type,index,value,raw_a,raw_b\n",controls_csv);
        }
        else if(!strcmp(argv[i],"--no-adaptive")) adaptive=0;
        else if(!strcmp(argv[i],"--erp4-spike-guard")) erp4_spike_guard=1;
        else if(!strcmp(argv[i],"--erp4-spike-min") && i+1<argc) erp4_spike_min=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--erp4-spike-ms") && i+1<argc) erp4_spike_ms=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--adaptive-ms") && i+1<argc) adaptive_ms=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--adaptive-tolerance") && i+1<argc) adaptive_tolerance=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--filter-csv") && i+1<argc) {
            const char *path=argv[++i]; filter_csv=fopen(path,"w");
            if(!filter_csv) {perror(path);return 2;}
            setvbuf(filter_csv,NULL,_IOLBF,0);
            fputs("timestamp_ms,erp,event,delta,emitted,suppressed_total\n",filter_csv);
        }
        else if(!strcmp(argv[i],"--raw-erp")) raw_erp=1;
        else if(!strcmp(argv[i],"--raw-packets")) raw_packets=1;
        else if(!strcmp(argv[i],"--no-midi")) no_midi=1;
        else if(!strcmp(argv[i],"--csv") && i+1<argc) {
            const char *path=argv[++i];
            csv=fopen(path,"w");
            if(!csv) {perror(path);return 2;}
            setvbuf(csv,NULL,_IOLBF,0);
            fputs("timestamp_ms,packet,erp,byte_a_index,byte_a,byte_b_index,byte_b,value,previous,delta,above_threshold\n",csv);
        }
        else if(!strcmp(argv[i],"--diagnose-reversals")) diagnose_reversals=1;
        else if(!strcmp(argv[i],"--reversal-ms") && i+1<argc) reversal_window_ms=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--reversal-tolerance") && i+1<argc) reversal_tolerance=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--poll-ms") && i+1<argc) poll_ms=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--erp-threshold") && i+1<argc) threshold=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--help")) {usage(argv[0]);return 0;}
        else {usage(argv[0]);return 2;}
    }
    if(audio_out_96 && (!audio_duplex || !audio_out_fixed || !audio_tx_independent || (!hp_tone && !pcm_fifo_mode) || audio_independent_short || audio_start_prime)) {fprintf(stderr,"--audio-out-96 requires --headphone-tone or --pcm-fifo (independent fixed OUT)\n");return 2;}
    if(audio_checkbyte_pattern && (!audio_duplex || !audio_out_fixed || !audio_tx_independent)) {fprintf(stderr,"--audio-checkbyte-zero requires independent fixed-length OUT diagnostic\n");return 2;}
    if(hp_tone) {puts("WARNING: experimental PCM mapping, low amplitude; headphones OFF ears, volume MINIMUM. Pattern timing begins at ISO OUT start.");hp_start_ms=clock_ms();}
    if(audio_echo_short && (!audio_duplex || audio_out_fixed || hp_tone)) {fprintf(stderr,"--audio-echo-short requires short silence duplex, no fixed OUT or tone\n");return 2;}
    if(audio_independent_short && audio_out_fixed) {fprintf(stderr,"Cannot combine short independent and fixed OUT\n");return 2;}
    if(audio_tx_independent && (!audio_duplex || (!audio_out_fixed && !audio_independent_short) || audio_start_prime)) {fprintf(stderr,"--audio-tx-independent requires --audio-duplex-silence --audio-out-fixed and excludes --audio-start-prime\n");return 2;}
    if(audio_start_prime && (!audio_duplex || !audio_out_fixed)) {fprintf(stderr,"--audio-start-prime requires --audio-duplex-silence --audio-out-fixed\n");return 2;}
    if(audio_out_fixed && !audio_duplex) {fprintf(stderr,"--audio-out-fixed requires --audio-duplex-silence\n");return 2;}
    if((audio_probe && (lcd_test || led_test_index>=0)) || (audio_probe && (audio_report_ms<100 || audio_report_ms>60000)) || (mixxx_feedback && (lcd_test || led_test_index>=0)) || (lcd_test && led_test_index>=0) || lcd_contrast_pct<0 || lcd_contrast_pct>100 || lcd_backlight_pct<0 || lcd_backlight_pct>100 || lcd_duration_ms<100 || lcd_duration_ms>30000 || led_test_level<0 || led_test_level>63 || led_test_duration_ms<100 || led_test_duration_ms>10000 || analog_cc_base<0 || analog_cc_base>125 || analog_min_change<1 || analog_min_change>4095 || erp4_spike_min<1 || erp4_spike_min>500 || erp4_spike_ms<10 || erp4_spike_ms>1000 || (erp4_spike_guard && !adaptive) || adaptive_ms<10 || adaptive_ms>1000 || adaptive_tolerance<0 || adaptive_tolerance>100 ||
       (adaptive && diagnose_reversals) || poll_ms<0 || (poll_ms>0 && poll_ms<10) || poll_ms>5000 || threshold<1 || threshold>500 || reversal_window_ms<10 || reversal_window_ms>2000 || reversal_tolerance<0 || reversal_tolerance>100) {
        fprintf(stderr,"Invalid options (adaptive and --diagnose-reversals are mutually exclusive)\n");return 2;
    }
    signal(SIGINT,on_signal); signal(SIGTERM,on_signal);
    start_ms=clock_ms();
    audio_trace_origin=start_ms;
    TRACE_STARTUP("process_started");
    if(filter_csv) fprintf(filter_csv,"%llu,0,session_start,0,0,0\n",(unsigned long long)start_ms);
    OSStatus s=MIDIClientCreate(CFSTR("KORE 1 Bridge"),NULL,NULL,&client);
    if(s!=noErr) {fprintf(stderr,"MIDIClientCreate: %d\n",(int)s);goto done;}
    s=MIDISourceCreate(client,CFSTR("KORE 1 Bridge"),&source);
    if(s!=noErr) {fprintf(stderr,"MIDISourceCreate: %d\n",(int)s);goto done;}
    if(mixxx_feedback) {
        s=MIDIDestinationCreate(client,CFSTR("KORE 1 Feedback"),feedback_midi_callback,NULL,&feedback_destination);
        if(s!=noErr) {fprintf(stderr,"MIDIDestinationCreate: %d\n",(int)s);goto done;}
    }
    TRACE_STARTUP("CoreMIDI initialized");
    int r=libusb_init(&usb_ctx);
    if(r) {fprintf(stderr,"libusb_init: %s\n",libusb_error_name(r));goto done;}
    device=libusb_open_device_with_vid_pid(usb_ctx,VID,PID);
    if(!device) {fprintf(stderr,"KORE 1 (17CC:4711) not found\n");goto done;}
    if(audio_startup_trace) {
        struct libusb_device_descriptor dd;
        libusb_device *ud=libusb_get_device(device);
        int dr=libusb_get_device_descriptor(ud,&dd);
        if(!dr) TRACE_STARTUP("USB device vid=%04x pid=%04x bcdUSB=%04x bcdDevice=%04x configurations=%u",dd.idVendor,dd.idProduct,dd.bcdUSB,dd.bcdDevice,dd.bNumConfigurations);
        struct libusb_config_descriptor *cfg=NULL;
        dr=libusb_get_active_config_descriptor(ud,&cfg);
        if(!dr && cfg) {
            TRACE_STARTUP("USB active config value=%u interfaces=%u",cfg->bConfigurationValue,cfg->bNumInterfaces);
            for(int ii=0;ii<cfg->bNumInterfaces;ii++) for(int aa=0;aa<cfg->interface[ii].num_altsetting;aa++) {
                const struct libusb_interface_descriptor *alt=&cfg->interface[ii].altsetting[aa];
                TRACE_STARTUP("USB interface=%u alt=%u class=%02x subclass=%02x endpoints=%u",alt->bInterfaceNumber,alt->bAlternateSetting,alt->bInterfaceClass,alt->bInterfaceSubClass,alt->bNumEndpoints);
                for(int ee=0;ee<alt->bNumEndpoints;ee++) {
                    const struct libusb_endpoint_descriptor *ep=&alt->endpoint[ee];
                    TRACE_STARTUP("USB endpoint=%02x attributes=%02x max_packet=%u interval=%u",ep->bEndpointAddress,ep->bmAttributes,ep->wMaxPacketSize,ep->bInterval);
                }
            }
            libusb_free_config_descriptor(cfg);
        } else TRACE_STARTUP("USB active config unavailable err=%d",dr);
    }
    TRACE_STARTUP("USB open successful; claiming interface=0");
    r=libusb_claim_interface(device,0);
    if(r) {fprintf(stderr,"claim interface 0: %s\n",libusb_error_name(r));goto done;}
    claimed=1;
    TRACE_STARTUP("USB interface claimed; setting alt=1");
    r=libusb_set_interface_alt_setting(device,0,1);
    if(r) {fprintf(stderr,"alt setting 1: %s\n",libusb_error_name(r));goto done;}
    TRACE_STARTUP("USB alt=1 selected");
    rx_transfer=libusb_alloc_transfer(0);
    if(!rx_transfer) {fprintf(stderr,"libusb_alloc_transfer failed\n");goto done;}
    libusb_fill_bulk_transfer(rx_transfer,device,EP_IN,rx,BUFSIZE,on_rx,NULL,0);
    r=libusb_submit_transfer(rx_transfer);
    if(r) {fprintf(stderr,"submit: %s\n",libusb_error_name(r));goto done;}
    submitted=1;
    TRACE_STARTUP("bulk IN armed; sending GET_DEVICE_INFO and AUTO_MSG");
    {const unsigned char info[]={0x01}; const unsigned char automsg[]={0x0B,0x01,0x0A,0x05};
     if(send_usb(info,sizeof(info)) || send_usb(automsg,sizeof(automsg))) goto done;}
    puts("KORE 1 connected. Virtual MIDI source: KORE 1 Bridge (CoreMIDI)");
    printf("ERP CC20-27 (relative), buttons notes36-67, analog CC%d-%d. Ctrl+C to stop.\n",analog_cc_base,analog_cc_base+2);
    if(led_test_index>=0) {
        if(run_led_test()) goto done;
        rc=0;
        goto done;
    }
    if(lcd_test) {
        if(run_lcd_test()) goto done;
        rc=0;
        goto done;
    }
    if(mixxx_feedback) {
        if(feedback_initialize()) goto done;
        puts("Mixxx feedback enabled: MIDI destination KORE 1 Feedback (channel 16 CC + SysEx).");
    }
    if(audio_init && run_audio_init()) goto done;
    TRACE_STARTUP("audio_init_finished ack=%d bpp=%d",audio_ack,audio_bpp);
    if(audio_out_fixed) {
        if(audio_alignment!=2 || audio_streams!=2 || audio_bpp!=176) {
            fprintf(stderr,"AUDIO fixed OUT safety stop: expected alignment=2 streams=2 bpp=176.\n");
            goto done;
        }
        printf("AUDIO EXPERIMENT: OUT length=%u per microframe; AUDIO_PARAMS bpp=%d; DAC interpretation unverified.\n",audio_out_96?96u:(unsigned)audio_bpp,audio_bpp);
    }
    if(audio_linux_trace) {
        printf("AUDIO LINUX TRACE: configured alignment=%d streams=%d bpp=%d, IN_URBs=%d, OUT=%s, scheduling=%s\n",
            audio_alignment,audio_streams,audio_bpp,audio_linux_urbs?32:1,
            audio_duplex?"enabled":"disabled",audio_tx_independent?"independent":audio_start_prime?"prime-before-IN":"RX-completion-driven");
        if(audio_duplex && !audio_tx_independent && !audio_start_prime && !audio_out_fixed)
            puts("AUDIO LINUX TRACE: Linux-like RX-matched OUT lengths; short 2-byte IN frames will produce short 2-byte OUT frames.");
        else puts("AUDIO LINUX TRACE: experimental scheduling differs from Linux's RX-matched OUT path.");
    }
    if(audio_duplex && audio_prepare_out(device)) goto done;
    TRACE_STARTUP("iso_start mode=%s",audio_tx_independent?"independent":audio_start_prime?"prime-before-IN":"RX-matched");
    if(audio_start_prime && audio_prime_out_before_in()) goto done;
    if(audio_tx_independent) printf("AUDIO v0.23 OUT checkbyte mode=%s (alignment=%d, streams=%d)\n",(const char*[]){"linux","zero","phase","swap","fixed13","fixed02"}[audio_checkbyte_pattern],audio_alignment,audio_streams);
    if(pcm_fifo_mode) {
        pcm_fd=open("/tmp/kore1-pcm.fifo",O_RDONLY|O_NONBLOCK);
        if(pcm_fd<0) {perror("open /tmp/kore1-pcm.fifo");goto done;}
        fprintf(stderr,"PCM FIFO active (48kHz stereo float32); experimental output; cap -12dBFS\n");
    }
    if(audio_tx_independent) { hp_audio_start_ms=clock_ms(); if(audio_start_independent_out()) goto done; }
    if(audio_probe && start_audio_probe(device)) goto done;
    if(!poll_ms) puts("AUTO_MSG-only test: no diagnostic polling.");
    if(adaptive) printf("Adaptive filter ON: window=%d ms, tolerance=%d units\n",adaptive_ms,adaptive_tolerance);
    if(diagnose_reversals) printf("Reversal diagnostics ON: window=%d ms, tolerance=%d units\n",reversal_window_ms,reversal_tolerance);
    struct timeval last,now,tv={0,50000}; gettimeofday(&last,NULL);
    while(running) {
        r=libusb_handle_events_timeout(usb_ctx,&tv);
        if(r && r!=LIBUSB_ERROR_INTERRUPTED) {fprintf(stderr,"USB events: %s\n",libusb_error_name(r));goto done;}
        if(mixxx_feedback && feedback_pump()) goto done;
        audio_report(0);
        if(adaptive) {flush_erp4_spike(clock_ms(),0); flush_adaptive(clock_ms(),0);}
        if(diagnose_reversals) flush_pending(clock_ms(),0);
        if(poll_ms) {
            gettimeofday(&now,NULL);
            if(elapsed_ms(&last,&now)>=poll_ms) {
                const unsigned char io[]={0x04},an[]={0x03};
                if(send_usb(io,1) || send_usb(an,1)) goto done;
                last=now;
            }
        }
    }
    if(adaptive) {flush_erp4_spike(clock_ms(),1); flush_adaptive(clock_ms(),1);}
    if(diagnose_reversals) flush_pending(clock_ms(),1);
    rc=0;
done:
    if(audio_probe) audio_report(1);
    fprintf(stderr,"V20_TIMING first_in_ms=%llu last_in_ms=%llu first_out_ms=%llu last_out_ms=%llu echo_updates=%llu echo_sends=%llu latest_in=%02X%02X\n",
       (unsigned long long)audio_first_in_ms,(unsigned long long)audio_last_in_ms,
       (unsigned long long)audio_first_out_ms,(unsigned long long)audio_last_out_ms,
       (unsigned long long)audio_echo_updates,(unsigned long long)audio_echo_sends,
       audio_latest_short[0],audio_latest_short[1]);
    if(filter_csv) fprintf(filter_csv,"%llu,0,session_end,0,0,0\n",(unsigned long long)clock_ms());
    if(verbose) {
        printf("Session duration: %.1f s, ERP packets: %llu\n",(clock_ms()-start_ms)/1000.0,(unsigned long long)erp_packet_count);
        for(int i=0;i<8;i++) printf("ERP %d MIDI messages: %lu\n",i+1,midi_erp_messages[i]);
    }
    running=0;
    if(pcm_fifo_mode) fprintf(stderr,"PCM FIFO frames=%llu underflows=%llu\n",(unsigned long long)pcm_frames_read,(unsigned long long)pcm_underflows);
    if(pcm_fd>=0) {close(pcm_fd);pcm_fd=-1;}
    if(hp_tone) fprintf(stderr,"HP_TONE samples=%llu phase_packets_silence=%llu phase_packets_440=%llu phase_packets_880=%llu\n",(unsigned long long)hp_tone_samples,(unsigned long long)hp_packet_counts[0],(unsigned long long)hp_packet_counts[1],(unsigned long long)hp_packet_counts[2]);
    if(audio_tx_independent) fprintf(stderr,"AUDIO v0.15 checkbytes generated=%llu mode=%s\n",(unsigned long long)audio_checkbytes_written,(const char*[]){"linux","zero","phase","swap","fixed13","fixed02"}[audio_checkbyte_pattern]);
    if(audio_tx_independent) fprintf(stderr,"AUDIO v0.15 independent OUT rearm stats: successful=%llu failed=%llu\n",(unsigned long long)audio_tx_rearms,(unsigned long long)audio_tx_rearm_errors);
    if(submitted && rx_transfer && !transfer_done) {
        r=libusb_cancel_transfer(rx_transfer);
        if(r && r!=LIBUSB_ERROR_NOT_FOUND) fprintf(stderr,"cancel: %s\n",libusb_error_name(r));
        while(!transfer_done) {
            struct timeval wait={0,100000};
            r=libusb_handle_events_timeout(usb_ctx,&wait);
            if(r && r!=LIBUSB_ERROR_INTERRUPTED) {fprintf(stderr,"cleanup USB: %s\n",libusb_error_name(r));break;}
        }
    }
    if(audio_submitted) {
        for(int i=0;i<audio_in_count;i++) {
            struct audio_in_slot *slot=&audio_in_slots[i];
            if(slot->active) {
                r=libusb_cancel_transfer(slot->transfer);
                if(r && r!=LIBUSB_ERROR_NOT_FOUND) fprintf(stderr,"AUDIO IN cancel #%d: %s\n",i,libusb_error_name(r));
            }
        }
        for(int tries=0;tries<40;tries++) {
            int active=0;
            for(int i=0;i<audio_in_count;i++) active+=audio_in_slots[i].active;
            if(!active) break;
            struct timeval wait={0,100000};
            r=libusb_handle_events_timeout(usb_ctx,&wait);
            if(r && r!=LIBUSB_ERROR_INTERRUPTED) break;
        }
    }
    for(int i=0;i<AUDIO_OUT_POOL;i++) {
        struct audio_out_slot *slot=&audio_out_slots[i];
        if(slot->transfer && slot->busy) {
            r=libusb_cancel_transfer(slot->transfer);
            if(r && r!=LIBUSB_ERROR_NOT_FOUND) fprintf(stderr,"AUDIO OUT cancel slot %d: %s\n",i,libusb_error_name(r));
        }
    }
    for(int tries=0;tries<40;tries++) {
        int busy=0;
        for(int i=0;i<AUDIO_OUT_POOL;i++) busy+=audio_out_slots[i].busy?1:0;
        if(!busy) break;
        struct timeval wait={0,100000};
        r=libusb_handle_events_timeout(usb_ctx,&wait);
        if(r && r!=LIBUSB_ERROR_INTERRUPTED) break;
    }
    for(int i=0;i<AUDIO_OUT_POOL;i++) {
        struct audio_out_slot *slot=&audio_out_slots[i];
        if(!slot->busy) {
            if(slot->transfer) libusb_free_transfer(slot->transfer);
            free(slot->buffer);
        }
    }
    for(int i=0;i<audio_in_count;i++) {
        struct audio_in_slot *slot=&audio_in_slots[i];
        if(!slot->active) {if(slot->transfer) libusb_free_transfer(slot->transfer); free(slot->buffer);}
    }
    if(rx_transfer && (!submitted || transfer_done)) libusb_free_transfer(rx_transfer);
    if(claimed) libusb_release_interface(device,0);
    if(device) libusb_close(device);
    if(usb_ctx) libusb_exit(usb_ctx);
    if(csv) fclose(csv);
    if(controls_csv) fclose(controls_csv);
    if(filter_csv) fclose(filter_csv);
    if(feedback_destination) MIDIEndpointDispose(feedback_destination);
    if(source) MIDIEndpointDispose(source);
    if(client) MIDIClientDispose(client);
    return rc;
}
