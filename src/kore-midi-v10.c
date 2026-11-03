/* Native Instruments KORE 1 -> virtual CoreMIDI source (macOS).
 * Build: cc -std=c11 -Wall -Wextra -O2 -o kore-midi-v6 kore-midi-v6.c $(pkg-config --cflags --libs libusb-1.0) -framework CoreMIDI -framework CoreFoundation
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

#define VID 0x17cc
#define PID 0x4711
#define EP_IN 0x81
#define EP_OUT 0x01
#define BUFSIZE 512
static volatile sig_atomic_t running=1;
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
            if(io_misc_last>=0) {
                int delta=misc-io_misc_last;
                if(delta>128)delta-=256;
                if(delta< -128)delta+=256;
                /* Ambiguous half-turn is not a direction. First packet is baseline. */
                if(delta!=128 && delta!= -128)while(delta) {
                    int step=abs(delta)>63?63:abs(delta);
                    midi3(0xB0,28,(unsigned char)(delta>0?step:64+step));
                    delta+=delta>0?-step:step;
                }
            }
            if((verbose || controls_raw) && misc!=io_misc_last) printf("IO misc: %d (raw=%u)\n",misc,(unsigned)p[4]);
            if(controls_csv && misc!=io_misc_last) fprintf(controls_csv,"%llu,misc,0,%d,%u,0\n",(unsigned long long)clock_ms(),misc,(unsigned)p[4]);
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
    if((mixxx_feedback && (lcd_test || led_test_index>=0)) || (lcd_test && led_test_index>=0) || lcd_contrast_pct<0 || lcd_contrast_pct>100 || lcd_backlight_pct<0 || lcd_backlight_pct>100 || lcd_duration_ms<100 || lcd_duration_ms>30000 || led_test_level<0 || led_test_level>63 || led_test_duration_ms<100 || led_test_duration_ms>10000 || analog_cc_base<0 || analog_cc_base>125 || analog_min_change<1 || analog_min_change>4095 || erp4_spike_min<1 || erp4_spike_min>500 || erp4_spike_ms<10 || erp4_spike_ms>1000 || (erp4_spike_guard && !adaptive) || adaptive_ms<10 || adaptive_ms>1000 || adaptive_tolerance<0 || adaptive_tolerance>100 ||
       (adaptive && diagnose_reversals) || poll_ms<0 || (poll_ms>0 && poll_ms<10) || poll_ms>5000 || threshold<1 || threshold>500 || reversal_window_ms<10 || reversal_window_ms>2000 || reversal_tolerance<0 || reversal_tolerance>100) {
        fprintf(stderr,"Invalid options (adaptive and --diagnose-reversals are mutually exclusive)\n");return 2;
    }
    signal(SIGINT,on_signal); signal(SIGTERM,on_signal);
    start_ms=clock_ms();
    if(filter_csv) fprintf(filter_csv,"%llu,0,session_start,0,0,0\n",(unsigned long long)start_ms);
    OSStatus s=MIDIClientCreate(CFSTR("KORE 1 Bridge"),NULL,NULL,&client);
    if(s!=noErr) {fprintf(stderr,"MIDIClientCreate: %d\n",(int)s);goto done;}
    s=MIDISourceCreate(client,CFSTR("KORE 1 Bridge"),&source);
    if(s!=noErr) {fprintf(stderr,"MIDISourceCreate: %d\n",(int)s);goto done;}
    if(mixxx_feedback) {
        s=MIDIDestinationCreate(client,CFSTR("KORE 1 Feedback"),feedback_midi_callback,NULL,&feedback_destination);
        if(s!=noErr) {fprintf(stderr,"MIDIDestinationCreate: %d\n",(int)s);goto done;}
    }
    int r=libusb_init(&usb_ctx);
    if(r) {fprintf(stderr,"libusb_init: %s\n",libusb_error_name(r));goto done;}
    device=libusb_open_device_with_vid_pid(usb_ctx,VID,PID);
    if(!device) {fprintf(stderr,"KORE 1 (17CC:4711) not found\n");goto done;}
    r=libusb_claim_interface(device,0);
    if(r) {fprintf(stderr,"claim interface 0: %s\n",libusb_error_name(r));goto done;}
    claimed=1;
    r=libusb_set_interface_alt_setting(device,0,1);
    if(r) {fprintf(stderr,"alt setting 1: %s\n",libusb_error_name(r));goto done;}
    rx_transfer=libusb_alloc_transfer(0);
    if(!rx_transfer) {fprintf(stderr,"libusb_alloc_transfer failed\n");goto done;}
    libusb_fill_bulk_transfer(rx_transfer,device,EP_IN,rx,BUFSIZE,on_rx,NULL,0);
    r=libusb_submit_transfer(rx_transfer);
    if(r) {fprintf(stderr,"submit: %s\n",libusb_error_name(r));goto done;}
    submitted=1;
    {const unsigned char info[]={0x01}; const unsigned char automsg[]={0x0B,0x01,0x0A,0x05};
     if(send_usb(info,sizeof(info)) || send_usb(automsg,sizeof(automsg))) goto done;}
    puts("KORE 1 connected. Virtual MIDI source: KORE 1 Bridge");
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
    if(!poll_ms) puts("AUTO_MSG-only test: no diagnostic polling.");
    if(adaptive) printf("Adaptive filter ON: window=%d ms, tolerance=%d units\n",adaptive_ms,adaptive_tolerance);
    if(diagnose_reversals) printf("Reversal diagnostics ON: window=%d ms, tolerance=%d units\n",reversal_window_ms,reversal_tolerance);
    struct timeval last,now,tv={0,50000}; gettimeofday(&last,NULL);
    while(running) {
        r=libusb_handle_events_timeout(usb_ctx,&tv);
        if(r && r!=LIBUSB_ERROR_INTERRUPTED) {fprintf(stderr,"USB events: %s\n",libusb_error_name(r));goto done;}
        if(mixxx_feedback && feedback_pump()) goto done;
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
    if(filter_csv) fprintf(filter_csv,"%llu,0,session_end,0,0,0\n",(unsigned long long)clock_ms());
    if(verbose) {
        printf("Session duration: %.1f s, ERP packets: %llu\n",(clock_ms()-start_ms)/1000.0,(unsigned long long)erp_packet_count);
        for(int i=0;i<8;i++) printf("ERP %d MIDI messages: %lu\n",i+1,midi_erp_messages[i]);
    }
    running=0;
    if(submitted && rx_transfer && !transfer_done) {
        r=libusb_cancel_transfer(rx_transfer);
        if(r && r!=LIBUSB_ERROR_NOT_FOUND) fprintf(stderr,"cancel: %s\n",libusb_error_name(r));
        while(!transfer_done) {
            struct timeval wait={0,100000};
            r=libusb_handle_events_timeout(usb_ctx,&wait);
            if(r && r!=LIBUSB_ERROR_INTERRUPTED) {fprintf(stderr,"cleanup USB: %s\n",libusb_error_name(r));break;}
        }
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
