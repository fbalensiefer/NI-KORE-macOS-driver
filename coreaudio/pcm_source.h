/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef KORE_PCM_SOURCE_H
#define KORE_PCM_SOURCE_H
#include "pcm_ring.h"
#include "rate_match.h"
#include "../audio/caiaq.h"
struct kore_pcm_source {
    struct kore_pcm_ring *queue;
    uint64_t position[2],consumed;
    double amplitude;
    bool rate_match;struct kore_rate_match matcher;
    unsigned char bytes[6];
};
/* Playback gain is independent of the diagnostic oscillator's -60 dB cap.
 * Clamp before conversion; non-finite input/gain produces silence. */
#define KORE_PCM_MAX_GAIN 1.0
static inline int32_t kore_pcm_sample(double value,double gain) {
    if(!isfinite(value) || !isfinite(gain) || gain<0 || gain>KORE_PCM_MAX_GAIN)return 0;
    if(value>1)value=1;
    if(value< -1)value= -1;
    return (int32_t)(value*gain*8388607.0);
}
static inline unsigned char kore_pcm_byte(void *context,unsigned stream) {
    struct kore_pcm_source *s=context;unsigned pos=(unsigned)(s->position[stream]++%6);
    if(stream!=1)return 0; /* verified headphone path: logical outputs 3/4 */
    if(pos==0) {
        float values[2]={0,0};
        if(s->rate_match) {
            kore_rate_match_frame(&s->matcher,s->queue,values);s->consumed=s->matcher.input_frames;
        } else {size_t count=kore_pcm_pop(s->queue,values,1);s->consumed+=count;}
        for(unsigned ch=0;ch<2;ch++)caiaq_s24be(s->bytes+ch*3,kore_pcm_sample(values[ch],s->amplitude));
    }
    return s->bytes[pos];
}
#endif
