/* SPDX-License-Identifier: GPL-2.0-or-later
 * One HAL mix producer, one serialized consumer. No locks/allocation in IO.
 */
#ifndef KORE_PCM_RING_H
#define KORE_PCM_RING_H
#include <stdatomic.h>
#include <stdint.h>
#include <stddef.h>
#define KORE_PCM_CAPACITY 8192u
_Static_assert(ATOMIC_LLONG_LOCK_FREE==2,"Realtime PCM queue requires lock-free 64-bit atomics");
struct kore_pcm_ring {
    _Atomic uint64_t written,read,overruns,underruns;
    float samples[KORE_PCM_CAPACITY][2];
};
static inline size_t kore_pcm_push(struct kore_pcm_ring *q,const float *stereo,size_t frames) {
    uint64_t w=atomic_load_explicit(&q->written,memory_order_relaxed),r=atomic_load_explicit(&q->read,memory_order_acquire);
    if(w-r>KORE_PCM_CAPACITY || frames>KORE_PCM_CAPACITY-(w-r)) {atomic_fetch_add(&q->overruns,1);return 0;}
    for(size_t i=0;i<frames;i++)for(unsigned ch=0;ch<2;ch++)q->samples[(w+i)%KORE_PCM_CAPACITY][ch]=stereo[i*2+ch];
    atomic_store_explicit(&q->written,w+frames,memory_order_release);return frames;
}
static inline size_t kore_pcm_pop(struct kore_pcm_ring *q,float *stereo,size_t frames) {
    uint64_t r=atomic_load_explicit(&q->read,memory_order_relaxed),w=atomic_load_explicit(&q->written,memory_order_acquire);
    size_t count=w-r<frames?(size_t)(w-r):frames;
    if(w-r>KORE_PCM_CAPACITY)return 0;
    for(size_t i=0;i<count;i++)for(unsigned ch=0;ch<2;ch++)stereo[i*2+ch]=q->samples[(r+i)%KORE_PCM_CAPACITY][ch];
    atomic_store_explicit(&q->read,r+count,memory_order_release);
    if(count<frames)atomic_fetch_add(&q->underruns,frames-count);
    return count;
}
#endif
