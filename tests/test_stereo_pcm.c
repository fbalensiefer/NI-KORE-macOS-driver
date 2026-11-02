#include "../coreaudio/pcm_source.h"
#include <assert.h>
#include <stdio.h>
static int32_t decode(const unsigned char *b) {
    uint32_t x=((uint32_t)b[0]<<16)|((uint32_t)b[1]<<8)|b[2];
    return x&0x800000?(int32_t)(x-0x1000000):(int32_t)x;
}
static void four_channels(double main,double headphones) {
    struct kore_pcm_ring q={.channels=4};float input[64*4];
    for(unsigned i=0;i<64;i++)for(unsigned ch=0;ch<4;ch++)input[i*4+ch]=(float)((i+1)*(ch+1))/512;
    assert(kore_pcm_push(&q,input,64)==64);
    struct kore_pcm_source s={.queue=&q,.position={4,4},.amplitude=main,.headphoneAmplitude=headphones,.separate_gains=true};
    unsigned cursor[2]={4,4};unsigned char sample[2][6]={{0}};
    for(unsigned packet=0;cursor[1]<6*65;packet++) {
        unsigned char buffer[176];unsigned length=packet%3?176:0;
        assert(!caiaq_pack(buffer,sizeof(buffer),length,kore_pcm_byte,&s));
        for(unsigned b=0;b<length;b++) {
            if(b%8==4 || b%8==5)continue;
            unsigned stream=b%2,pos=cursor[stream]++,part=pos%6;
            sample[stream][part]=buffer[b];
            if(part%3==2) {
                int frame=(int)(pos/6)-1;unsigned ch=stream*2+part/3;
                int32_t expected=frame>=0 && frame<64?(int32_t)(input[frame*4+ch]*(ch<2?main:headphones)*8388607.):0;
                assert(decode(sample[stream]+(part/3)*3)==expected);
            }
        }
    }
    assert(s.consumed==64); /* one pop per four-channel frame, not per stream */
    struct kore_rate_match match;kore_rate_match_init(&match,8);
    assert(kore_pcm_push(&q,input,64)==64);
    float out[4];
    for(unsigned i=0;i<30;i++) {
        assert(kore_rate_match_frame(&match,&q,out));
        assert(fabs(out[1]-2*out[0])<1e-7 && fabs(out[2]-3*out[0])<1e-7 && fabs(out[3]-4*out[0])<1e-7);
    }
}
int main(void) {
    four_channels(1,1);four_channels(.2,1);four_channels(0,.1);four_channels(.5,0);
    for(unsigned gain=0;gain<5;gain++) {
        double amplitude=(double[]){.00025,.001,.01,.1,1}[gain];struct kore_pcm_ring q={0};float input[128];
        for(unsigned i=0;i<64;i++) {
            input[i*2]=i<16?(float)sin(i*.2):i<32?0:i<48?1:NAN;
            input[i*2+1]=i<16?0:i<32?(float)sin(i*.3):i<48?-.5:INFINITY;
        }
        assert(kore_pcm_push(&q,input,64)==64);
        struct kore_pcm_source s={.queue=&q,.position={4,4},.amplitude=amplitude};
        unsigned cursor[2]={4,4};unsigned char samples[2][6]={{0}};
        for(unsigned packet=0;cursor[1]<6*67;packet++) {
            unsigned length=packet%3?176:0;unsigned char buffer[176];
            assert(!caiaq_pack(buffer,sizeof(buffer),length,kore_pcm_byte,&s));
            for(unsigned byte=0;byte<length;byte++) {
                if(byte%8==4 || byte%8==5)continue;
                unsigned stream=byte%2,pos=cursor[stream]++,part=pos%6,ch=part/3;
                samples[stream][part]=buffer[byte];
                if(part%3==2) {
                    int frame=(int)(pos/6)-1;int32_t expected=0;
                    if(stream==1 && frame>=0 && frame<64)expected=isfinite(input[frame*2+ch])?(int32_t)(input[frame*2+ch]*amplitude*8388607.):0;
                    assert(decode(samples[stream]+ch*3)==expected);
                }
            }
        }
        assert(s.consumed==64);
    }
    assert(kore_pcm_sample(2,.1)==838860);
    assert(kore_pcm_sample(-2,1)==-8388607);
    assert(kore_pcm_sample(.5,NAN)==0);
    assert(kore_pcm_sample(.5,1.01)==0);
    puts("PASS stereo PCM wire decode: exact independent L/R samples, inactive outputs, five playback gains and non-finite silence");
}
