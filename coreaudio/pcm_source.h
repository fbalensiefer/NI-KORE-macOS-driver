/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef KORE_PCM_SOURCE_H
#define KORE_PCM_SOURCE_H
#include "pcm_ring.h"
#include "../audio/caiaq.h"
struct kore_pcm_source {
    struct kore_pcm_ring *queue;
    uint64_t position[2],consumed;
    double amplitude;
    unsigned char bytes[6];
};
static inline unsigned char kore_pcm_byte(void *context,unsigned stream) {
    struct kore_pcm_source *s=context;unsigned pos=(unsigned)(s->position[stream]++%6);
    if(stream!=1)return 0; /* verified headphone path: logical outputs 3/4 */
    if(pos==0) {
        float values[2]={0,0};size_t count=kore_pcm_pop(s->queue,values,1);s->consumed+=count;
        for(unsigned ch=0;ch<2;ch++)caiaq_s24be(s->bytes+ch*3,caiaq_safe_sample(values[ch]*s->amplitude,s->amplitude));
    }
    return s->bytes[pos];
}
#endif
