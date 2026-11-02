/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bounded occupancy-controlled linear ASRC. Input host PCM -> device-paced frames.
 * Changes PCM interpolation only; never changes USB packet lengths or schedule.
 */
#ifndef KORE_RATE_MATCH_H
#define KORE_RATE_MATCH_H
#include "pcm_ring.h"
#include <math.h>
#include <stdbool.h>
struct kore_rate_match {
    unsigned target,prime_target,control_frames,warmup_frames,calibration_samples;
    double calibration_sum;
    bool primed;
    double phase,ratio,integral,filtered_error,fade;
    float left[2],right[2],last[2];
    uint64_t input_frames,output_frames,priming_frames,starvations;
};
static inline void kore_rate_match_init(struct kore_rate_match *s,unsigned target) {
    *s=(struct kore_rate_match){.target=target,.prime_target=target,.ratio=1};
}
static inline uint64_t kore_pcm_available(struct kore_pcm_ring *q) {
    uint64_t w=atomic_load_explicit(&q->written,memory_order_acquire);
    uint64_t r=atomic_load_explicit(&q->read,memory_order_relaxed);
    return w-r<=KORE_PCM_CAPACITY?w-r:0;
}
static inline float kore_finite_sample(float value) {
    if(!isfinite(value))return 0;
    return value>1?1:value< -1?-1:value;
}
static inline bool kore_rate_match_frame(struct kore_rate_match *s,struct kore_pcm_ring *q,float out[2]) {
    s->output_frames++;
    if(!s->primed) {
        if(s->prime_target && s->prime_target<KORE_PCM_CAPACITY-2 && kore_pcm_available(q)>=s->prime_target+2) {
            float pair[4];if(kore_pcm_pop(q,pair,2)!=2)return false;
            for(unsigned ch=0;ch<2;ch++){s->left[ch]=kore_finite_sample(pair[ch]);s->right[ch]=kore_finite_sample(pair[ch+2]);}
            s->input_frames+=2;s->phase=0;s->fade=0;s->filtered_error=0;s->primed=true;
            s->control_frames=s->warmup_frames=s->calibration_samples=0;s->calibration_sum=0;s->integral=0;s->ratio=1;
        } else {
            s->priming_frames++;s->fade=fmax(0,s->fade-1./240);
            for(unsigned ch=0;ch<2;ch++)out[ch]=(float)(s->last[ch]*s->fade);
            return false;
        }
    }
    /* Once per 480 output frames (~10 ms). One-second occupancy filtering
     * rejects producer block jitter; PI settles sustained +/- clock error. */
    if(++s->control_frames==480) {
        s->control_frames=0;
        if(s->warmup_frames<48000) {
            /* Estimate the actual startup reservoir for one second. Do not
             * mistake USB startup latency / producer block phase for drift. */
            s->warmup_frames+=480;s->calibration_sum+=kore_pcm_available(q);s->calibration_samples++;
            if(s->warmup_frames==48000)s->target=(unsigned)(s->calibration_sum/s->calibration_samples+.5);
        } else {
            double error=(double)kore_pcm_available(q)-s->target;
            s->filtered_error+=.01*(error-s->filtered_error);
            double integral=s->integral+2.5e-6*s->filtered_error*.01;
            if(integral>.001)integral=.001;if(integral< -.001)integral= -.001;
            s->integral=integral;
            double correction=1e-5*s->filtered_error+integral;
            if(correction>.001)correction=.001;if(correction< -.001)correction= -.001;
            s->ratio=1+correction;
        }
    }
    s->fade=fmin(1,s->fade+1./240);
    for(unsigned ch=0;ch<2;ch++) {
        float value=(float)(s->left[ch]+(s->right[ch]-s->left[ch])*s->phase);
        s->last[ch]=value;out[ch]=(float)(value*s->fade);
    }
    s->phase+=s->ratio;
    while(s->phase>=1) {
        float next[2];
        if(kore_pcm_pop(q,next,1)!=1){s->starvations++;s->primed=false;break;}
        for(unsigned ch=0;ch<2;ch++){s->left[ch]=s->right[ch];s->right[ch]=kore_finite_sample(next[ch]);}
        s->input_frames++;s->phase-=1;
    }
    return true;
}
#endif
