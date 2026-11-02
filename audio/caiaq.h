/* SPDX-License-Identifier: GPL-2.0-or-later
 * Alignment-2 framing follows Linux v6.12 sound/usb/caiaq/audio.c
 * by Daniel Mack and Karsten Wiese. Packet check phase resets; PCM cursor does not.
 */
#ifndef KORE_CAIAQ_H
#define KORE_CAIAQ_H
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#define CAIAQ_MAX_PACKET 512u
#define CAIAQ_MAX_PACKETS 8u
#define CAIAQ_MAX_AMPLITUDE 0.001 /* -60 dBFS */
/* Synthetic submission probes, never a playback clock or routing strategy. */
#define CAIAQ_PROBE_COUNT 7u
static const char *const caiaq_probe_names[CAIAQ_PROBE_COUNT]={
    "uniform96","uniform176","leading-zero","trailing-zero","interior-zero","observed","variable-nonzero"
};
static inline int caiaq_probe_id(const char *name) {
    for(unsigned i=0;i<CAIAQ_PROBE_COUNT;i++) if(!strcmp(name,caiaq_probe_names[i])) return (int)i;
    return -1;
}
static inline int caiaq_probe_layout(unsigned id,unsigned lengths[8]) {
    static const unsigned vectors[CAIAQ_PROBE_COUNT][8]={
        {96,96,96,96,96,96,96,96},
        {176,176,176,176,176,176,176,176},
        {0,176,176,176,176,176,176,176},
        {176,176,176,176,176,176,176,0},
        {176,176,176,0,176,176,176,176},
        {0,176,176,0,176,0,176,0},
        {96,176,96,176,96,176,96,176}
    };
    if(id>=CAIAQ_PROBE_COUNT || !lengths)return -1;
    memcpy(lengths,vectors[id],sizeof(vectors[id]));return 0;
}
typedef unsigned char (*caiaq_pcm_byte_fn)(void *, unsigned);
static inline int caiaq_packet_valid(unsigned n) {
    /* Complete Linux loop iterations: PCM pair, optional check pair. */
    return n<=CAIAQ_MAX_PACKET && (n%8==0 || n%8==2 || n%8==6);
}
static inline int caiaq_pack(unsigned char *dst, size_t capacity, unsigned n,
                             caiaq_pcm_byte_fn next, void *ctx) {
    if(!caiaq_packet_valid(n) || n>capacity || (!dst && n)) return -1;
    for(unsigned i=0;i<n;) {
        for(unsigned stream=0;stream<2;stream++,i++) dst[i]=next?next(ctx,stream):0;
        if(i%8==4)
            for(unsigned stream=0;stream<2;stream++,i++)
                dst[i]=(unsigned char)((stream<<1)|(~(i/8)&1u));
    }
    return 0;
}
/* Validate entire layout before filling anything; zeros retain their time slots. */
static inline int caiaq_layout(const unsigned *lengths, unsigned count,
                               size_t capacity, size_t *offsets, size_t *total) {
    size_t sum=0;
    if(!lengths || !offsets || !total || !count || count>CAIAQ_MAX_PACKETS) return -1;
    for(unsigned i=0;i<count;i++) {
        if(!caiaq_packet_valid(lengths[i]) || sum>capacity || lengths[i]>capacity-sum) return -1;
        sum+=lengths[i];
    }
    sum=0;
    for(unsigned i=0;i<count;i++) {offsets[i]=sum;sum+=lengths[i];}
    *total=sum;
    return 0;
}
static inline void caiaq_s24be(unsigned char dst[3], int32_t sample) {
    uint32_t u=(uint32_t)sample;
    dst[0]=(unsigned char)(u>>16);dst[1]=(unsigned char)(u>>8);dst[2]=(unsigned char)u;
}
static inline int32_t caiaq_safe_sample(double value, double amplitude) {
    if(!isfinite(value) || !isfinite(amplitude) || amplitude<0) return 0;
    if(amplitude>CAIAQ_MAX_AMPLITUDE) amplitude=CAIAQ_MAX_AMPLITUDE;
    if(value>amplitude)value=amplitude;
    if(value< -amplitude)value= -amplitude;
    return (int32_t)(value*8388607.0);
}
#endif
