#include "../audio/tone.h"
#include <assert.h>
#include <stdio.h>
static int32_t decode(const unsigned char b[3]) {
    uint32_t v=((uint32_t)b[0]<<16)|((uint32_t)b[1]<<8)|b[2];
    return v&0x800000?(int32_t)(v-0x1000000):(int32_t)v;
}
int main(void) {
    for(unsigned selected=1;selected<=4;selected++) {
        struct caiaq_tone tone;assert(!caiaq_tone_init(&tone,selected,.00025));
        uint64_t positions[2]={4,4};unsigned char samples[2][2][3]={0};
        int32_t previous=0;unsigned crossings=0;double power=0;unsigned measured=0;
        for(unsigned packet=0;positions[0]<144000*6+4;packet++) {
            unsigned n=(packet%11<6)?176:0;unsigned char buffer[178];memset(buffer,0xa5,sizeof(buffer));
            assert(!caiaq_pack(buffer+1,n,n,caiaq_tone_byte,&tone));
            assert(buffer[0]==0xa5 && buffer[n+1]==0xa5);
            for(unsigned i=0;i<n;i++) {
                if(i%8==4 || i%8==5)continue;
                unsigned stream=i%2;uint64_t pos=positions[stream]++;
                unsigned chan=(unsigned)(pos%6)/3,byte=(unsigned)(pos%3);
                samples[stream][chan][byte]=buffer[i+1];
                if(byte==2) {
                    int32_t sample=decode(samples[stream][chan]);uint64_t frame=pos/6;
                    assert(sample>=-2097 && sample<=2097);
                    if(stream*2+chan+1!=selected || frame<24000 || frame>=72000)assert(sample==0);
                    if(stream*2+chan+1==selected && frame>=24000 && frame<72000) {
                        if(previous<=0 && sample>0)crossings++;previous=sample;
                        power+=(double)sample*sample;measured++;
                    }
                }
            }
        }
        assert(crossings>=439 && crossings<=441 && measured==48000);
        double rms=sqrt(power/measured)/8388607.;assert(rms>.00017 && rms<.00018);
        assert(tone.generated==48000 && tone.nonzero>47000);
    }
    struct caiaq_tone t;assert(caiaq_tone_init(&t,0,.00025)==-1);
    assert(caiaq_tone_init(&t,1,.002)==-1);assert(caiaq_tone_init(&t,1,NAN)==-1);
    puts("PASS native tone: all four single-channel mappings, 440Hz, 48kHz PCM, signed24, silence windows, fades, amplitude ceiling, 0/176 continuity");
}
