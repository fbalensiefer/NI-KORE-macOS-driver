/* Exercise production functions and callbacks without initializing USB or MIDI. */
#include <libusb.h>
static int fake_submit(struct libusb_transfer *t);
#define libusb_submit_transfer fake_submit
#define main bridge_main
#include "../kore1-bridge-audio.c"
#undef main
#undef libusb_submit_transfer
#include <assert.h>
static int submit_result, submits;
static unsigned submitted_lengths[8];
static int fake_submit(struct libusb_transfer *t) {
    submits++;
    size_t sum=0;
    for(int i=0;i<t->num_iso_packets;i++) {submitted_lengths[i]=t->iso_packet_desc[i].length;sum+=submitted_lengths[i];}
    assert(sum==(size_t)t->length);
    return submit_result;
}
struct cursor {unsigned pos[2];};
static unsigned char next_byte(void *ctx,unsigned stream) {
    struct cursor *c=ctx;return (unsigned char)(stream*97+c->pos[stream]++);
}
/* Independent position-based oracle: PCM at offsets 0,2,6 and 1,3,7. */
static void framing(void) {
    struct cursor c={{0,0}};unsigned expect[2]={0,0};
    const unsigned sizes[]={96,176,0,176,0,96,2,6,512};
    for(unsigned p=0;p<sizeof(sizes)/sizeof(*sizes);p++) {
        unsigned n=sizes[p];unsigned char storage[514];memset(storage,0xa5,sizeof(storage));
        assert(caiaq_pack(storage+1,n,n,next_byte,&c)==0);
        assert(storage[0]==0xa5 && storage[n+1]==0xa5);
        for(unsigned i=0;i<n;i++) {
            unsigned off=i%8,stream=i%2;
            unsigned char expected=(off==4 || off==5)?(unsigned char)(2*stream+((i/8)%2==0)):
                (unsigned char)(stream*97+expect[stream]++);
            assert(storage[i+1]==expected);
        }
    }
    unsigned char b[512];memset(b,0xa5,sizeof(b));
    assert(caiaq_pack(b,95,96,NULL,NULL)==-1 && b[0]==0xa5);
    assert(caiaq_pack(b,512,4,NULL,NULL)==-1 && b[0]==0xa5);
    assert(caiaq_pack(b,512,513,NULL,NULL)==-1);
    assert(caiaq_pack(b,96,96,NULL,NULL)==0);
    const unsigned char silence[]={0,0,0,0,1,3,0,0,0,0,0,0,0,2,0,0};
    assert(!memcmp(b,silence,sizeof(silence)));
    const int32_t values[]={0,1,-1,8388607,-8388608};
    const unsigned char vectors[][3]={{0,0,0},{0,0,1},{255,255,255},{127,255,255},{128,0,0}};
    for(unsigned i=0;i<5;i++) {caiaq_s24be(b,values[i]);assert(!memcmp(b,vectors[i],3));}
    assert(caiaq_safe_sample(NAN,.001)==0);
    assert(caiaq_safe_sample(1,1)==8388);
    assert(caiaq_safe_sample(-1,1)==-8388);
}
static void layouts(void) {
    unsigned n[]={0,176,0,176,176,0,176,0};size_t offsets[8],total;
    assert(!caiaq_layout(n,8,4096,offsets,&total) && total==704);
    assert(offsets[0]==0 && offsets[1]==0 && offsets[2]==176 && offsets[7]==704);
    assert(caiaq_layout(n,8,703,offsets,&total)==-1);
    n[3]=513;assert(caiaq_layout(n,8,4096,offsets,&total)==-1);
    memset(n,0,sizeof(n));assert(!caiaq_layout(n,8,4096,offsets,&total) && total==0);
    assert(caiaq_layout(n,9,4096,offsets,&total)==-1);
}
static int32_t unpack(const unsigned char *b) {
    uint32_t v=((uint32_t)b[0]<<16)|((uint32_t)b[1]<<8)|b[2];
    return (v&0x800000)?(int32_t)(v-0x1000000):(int32_t)v;
}
static void waveform(void) {
    audio_alignment=2;audio_streams=2;pcm_fifo_mode=0;audio_max_amplitude=.00025;
    for(int selected=1;selected<=4;selected++) {
        audio_tone_channel=selected;linux_pcm_phase=0;
        memset(linux_pcm_sample_byte,0,sizeof(linux_pcm_sample_byte));
        memset(linux_pcm_sample,0,sizeof(linux_pcm_sample));
        unsigned char pending[2][6]={{0}};unsigned at[2]={0,0};
        int crossings=0,last=0;unsigned frames=0;
        for(unsigned p=0;p<8000;p++) {
            unsigned char b[96];linux_fill_pcm(b,96,1);
            for(unsigned i=0;i<96;i++) {
                if(i%8==4 || i%8==5)continue;
                unsigned stream=i%2;pending[stream][at[stream]++]=b[i];
                if(at[stream]==6) {
                    at[stream]=0;
                    for(unsigned chan=0;chan<2;chan++) {
                        int32_t sample=unpack(pending[stream]+chan*3);
                        assert(abs(sample)<=2097);
                        if((int)(stream*2+chan)==selected-1) {
                            if(last<=0 && sample>0)crossings++;
                            last=sample;frames++;
                        } else assert(sample==0);
                    }
                }
            }
        }
        assert(frames==48000 && crossings==440);
    }
}
static void scheduler(void) {
    struct libusb_transfer *rx=libusb_alloc_transfer(8),*out=libusb_alloc_transfer(8);
    assert(rx && out);rx->num_iso_packets=out->num_iso_packets=8;
    unsigned char buffer[4096];
    audio_out_slots[0].transfer=out;audio_out_slots[0].buffer=buffer;
    audio_duplex=audio_rx_matched_zero=1;audio_tx_independent=0;audio_out_fixed=0;hp_tone=0;
    out->user_data=&audio_out_slots[0];
    unsigned lens[]={0,176,0,176,176,0,176,0};
    for(int i=0;i<8;i++) {rx->iso_packet_desc[i].actual_length=lens[i];rx->iso_packet_desc[i].status=LIBUSB_TRANSFER_COMPLETED;}
    audio_send_silence_matching_rx(rx);assert(submits==1 && audio_out_slots[0].busy);
    assert(out->length==704 && !memcmp(submitted_lengths,lens,sizeof(lens)));
    audio_send_silence_matching_rx(rx);assert(submits==1); /* busy buffers cannot be reused */
    out->status=LIBUSB_TRANSFER_CANCELLED;on_audio_tx(out);assert(!audio_out_slots[0].busy);
    for(int i=0;i<8;i++)rx->iso_packet_desc[i].actual_length=0;
    audio_send_silence_matching_rx(rx);assert(submits==1);
    for(int i=0;i<8;i++)rx->iso_packet_desc[i].actual_length=lens[i];
    for(unsigned mask=1;mask<256;mask++) {
        for(int i=0;i<8;i++)rx->iso_packet_desc[i].actual_length=(mask&(1u<<i))?176:0;
        int before=submits;audio_send_silence_matching_rx(rx);assert(submits==before+1);
        out->status=LIBUSB_TRANSFER_CANCELLED;on_audio_tx(out);
    }
    for(int i=0;i<8;i++)rx->iso_packet_desc[i].actual_length=lens[i];
    int before_failure=submits;
    submit_result=LIBUSB_ERROR_INVALID_PARAM;running=1;
    audio_send_silence_matching_rx(rx);assert(submits==before_failure+1 && !running && audio_failed && !audio_out_slots[0].busy);
    running=1;audio_failed=0;rx->iso_packet_desc[2].actual_length=513;
    audio_send_silence_matching_rx(rx);assert(submits==before_failure+1 && !running && audio_failed);
    /* Exact logged failure from an IN callback: OUT failure must not rearm IN. */
    running=1;audio_failed=0;rx->iso_packet_desc[2].actual_length=0;
    rx->status=LIBUSB_TRANSFER_COMPLETED;rx->user_data=&audio_in_slots[0];
    audio_in_slots[0].active=1;audio_in_count=1;
    int before_rx=submits;on_audio_rx(rx);
    assert(submits==before_rx+1 && !running && !audio_in_slots[0].active);
    /* Each profile submits once, retains every descriptor, and stops on completion. */
    for(unsigned profile=0;profile<CAIAQ_PROBE_COUNT;profile++) {
        unsigned expected[8];size_t off[8],sum;
        assert(!caiaq_probe_layout(profile,expected));
        assert(caiaq_probe_id(caiaq_probe_names[profile])==(int)profile);
        assert(!caiaq_layout(expected,8,4096,off,&sum));
        audio_layout_probe=(int)profile;audio_layout_sent=audio_layout_completed=0;
        running=1;audio_failed=0;audio_tx_errors=0;submit_result=0;audio_rx_matched_zero=0;
        int before=submits;
        audio_send_silence_matching_rx(rx);
        assert(submits==before+1 && audio_layout_sent && out->length==(int)sum);
        assert(!memcmp(submitted_lengths,expected,sizeof(expected)));
        audio_send_silence_matching_rx(rx);assert(submits==before+1);
        for(int i=0;i<8;i++) {
            out->iso_packet_desc[i].status=LIBUSB_TRANSFER_COMPLETED;
            out->iso_packet_desc[i].actual_length=expected[i];
        }
        out->status=LIBUSB_TRANSFER_COMPLETED;on_audio_tx(out);
        assert(!running && audio_layout_completed && !audio_out_slots[0].busy);
        audio_send_silence_matching_rx(rx);assert(submits==before+1);
    }
    audio_layout_probe=5;audio_layout_sent=audio_layout_completed=0;
    running=1;audio_failed=0;submit_result=LIBUSB_ERROR_INVALID_PARAM;
    int before_probe_failure=submits;
    audio_send_silence_matching_rx(rx);
    assert(submits==before_probe_failure+1 && audio_layout_sent && !running && !audio_layout_completed);
    audio_send_silence_matching_rx(rx);assert(submits==before_probe_failure+1);
    unsigned observed[8],last_data[8];size_t positions[8],observed_total,last_total;
    assert(!caiaq_probe_layout((unsigned)caiaq_probe_id("observed"),observed));
    assert(!caiaq_probe_layout((unsigned)caiaq_probe_id("observed-last-data"),last_data));
    assert(!caiaq_layout(observed,8,4096,positions,&observed_total));
    assert(!caiaq_layout(last_data,8,4096,positions,&last_total));
    assert(observed_total==704 && last_total==observed_total);
    assert(!memcmp(observed,last_data,6*sizeof(unsigned)));
    assert(last_data[6]==observed[7] && last_data[7]==observed[6]);
    unsigned invalid[8];assert(caiaq_probe_layout(CAIAQ_PROBE_COUNT,invalid)==-1);
    assert(caiaq_probe_id("unknown")==-1);
    audio_layout_probe=-1;
    audio_out_slots[0].transfer=NULL;audio_out_slots[0].buffer=NULL;
    libusb_free_transfer(rx);libusb_free_transfer(out);
}
int main(void) {
    assert(linux_pcm_sample_byte[0]==4 && linux_pcm_sample_byte[1]==4);
    framing();layouts();waveform();scheduler();
    puts("PASS: Linux byte vectors, signed24, 4-channel 440Hz, continuity, bounds, ISO layouts, callback ownership and failure stop");
    return 0;
}
