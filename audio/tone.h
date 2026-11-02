/* SPDX-License-Identifier: GPL-2.0-or-later
 * Opt-in 440Hz, signed24 BE, single logical channel. No USB access.
 */
#ifndef KORE_TONE_H
#define KORE_TONE_H
#include "caiaq.h"
struct caiaq_tone {
    unsigned channel; /* 1..4 */
    double amplitude;
    uint64_t position[2],generated,nonzero;
};
static inline int caiaq_tone_init(struct caiaq_tone *tone,unsigned channel,double amplitude) {
    if(!tone || channel<1 || channel>4 || !isfinite(amplitude) || amplitude<0 || amplitude>CAIAQ_MAX_AMPLITUDE)return -1;
    *tone=(struct caiaq_tone){.channel=channel,.amplitude=amplitude,.position={4,4}};
    return 0;
}
static inline unsigned char caiaq_tone_byte(void *ctx,unsigned stream) {
    struct caiaq_tone *tone=ctx;
    uint64_t pos=tone->position[stream]++,frame=pos/6;
    unsigned channel=(unsigned)(pos%6)/3,byte=(unsigned)(pos%3);
    int32_t sample=0;
    /* 0.5 s silence, 1 s tone, then silence; 5 ms fades, fixed 48k PCM clock. */
    if(stream*2+channel+1==tone->channel && frame>=24000 && frame<72000) {
        uint64_t n=frame-24000;double envelope=1;
        if(n<240)envelope=(double)n/240;
        if(n>=48000-240)envelope=(double)(48000-1-n)/240;
        sample=caiaq_safe_sample(tone->amplitude*envelope*sin(6.2831853071795864769*440*(double)(n%1200)/48000),tone->amplitude);
        if(!byte){tone->generated++;if(sample)tone->nonzero++;}
    }
    unsigned char bytes[3];caiaq_s24be(bytes,sample);return bytes[byte];
}
#endif
