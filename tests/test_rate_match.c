#include "../coreaudio/rate_match.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
/* Simulated 512-frame host producer and device clock; no sleeps or hardware. */
static void simulate(double ppm,unsigned seconds) {
    struct kore_pcm_ring *q=calloc(1,sizeof(*q));assert(q);q->channels=4;
    struct kore_rate_match match;kore_rate_match_init(&match,2048);
    float initial[2050*4];for(unsigned i=0;i<2050*4;i++)initial[i]=.5f;
    assert(kore_pcm_push(q,initial,2050)==2050);
    double device_rate=48000*(1+ppm/1e6);uint64_t produced=2050,next_block=512,outputs=(uint64_t)(device_rate*seconds);
    unsigned minimum=8192,maximum=0;
    for(uint64_t n=0;n<outputs;n++) {
        uint64_t host_elapsed=(uint64_t)(n*48000/device_rate);
        if(host_elapsed>=next_block) {
            float block[512*4];for(unsigned i=0;i<512*4;i++)block[i]=.5f;
            assert(kore_pcm_push(q,block,512)==512);produced+=512;next_block+=512;
        }
        float out[4]={0};kore_rate_match_frame(&match,q,out);
        assert(isfinite(out[0]) && out[0]>=0 && out[0]<=.5f && out[0]==out[1] && out[1]==out[2] && out[2]==out[3]);
        assert(match.ratio>=.999 && match.ratio<=1.001);
        unsigned available=(unsigned)kore_pcm_available(q);
        if(n>48000*30u){if(available<minimum)minimum=available;if(available>maximum)maximum=available;}
    }
    double expected=48000/device_rate;
    printf("clock=%+.0fppm simulated=%us ratio=%.9f expected=%.9f range=%u..%u starvations=%llu\n",ppm,seconds,match.ratio,expected,minimum,maximum,(unsigned long long)match.starvations);
    assert(!match.starvations && atomic_load(&q->overruns)==0);
    assert(minimum>512 && maximum<4096);
    assert(fabs(match.ratio-expected)<.00015);
    assert(produced-match.input_frames==kore_pcm_available(q));free(q);
}
static void interpolation_vectors(void) {
    struct kore_pcm_ring q={0};float frames[1024];
    for(unsigned i=0;i<512;i++){frames[i*2]=(i+2)/1024.f;frames[i*2+1]=-(float)(i+2)/1024.f;}
    assert(kore_pcm_push(&q,frames,512)==512);
    struct kore_rate_match s={.target=8,.primed=true,.ratio=.9998,.fade=1,.left={0,0},.right={1/1024.f,-1/1024.f},.input_frames=2};
    for(unsigned n=0;n<400;n++) {
        float out[2];assert(kore_rate_match_frame(&s,&q,out));
        assert(fabs(out[0]-n*.9998/1024)<1e-7 && out[1]==-out[0]);
    }
}
int main(void) {
    interpolation_vectors();
    simulate(200,180);simulate(-200,180);simulate(500,180);simulate(-500,180);simulate(0,180);
    struct kore_pcm_ring q={0};struct kore_rate_match s;kore_rate_match_init(&s,8);float out[2];
    for(unsigned n=0;n<1000;n++){assert(!kore_rate_match_frame(&s,&q,out));assert(out[0]==0 && out[1]==0);}
    assert(s.integral==0 && s.ratio==1);float data[32];for(unsigned i=0;i<32;i++)data[i]=NAN;
    assert(kore_pcm_push(&q,data,16)==16);
    for(unsigned n=0;n<1000;n++){kore_rate_match_frame(&s,&q,out);assert(isfinite(out[0]) && isfinite(out[1]));}
    assert(s.starvations==1);
    float recovery[1024];for(unsigned i=0;i<1024;i++)recovery[i]=.5f;
    assert(kore_pcm_push(&q,recovery,512)==512);
    for(unsigned i=0;i<100;i++){assert(kore_rate_match_frame(&s,&q,out));assert(out[0]>=0 && out[0]<=.5f);}
    assert(s.starvations==1 && s.warmup_frames==0 && s.integral==0);
    puts("PASS adaptive rate matching: 15 simulated minutes, +/-500ppm, block jitter, bounded occupancy, starvation and finite output");
}
