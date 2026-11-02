/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef KORE_NATIVE_PLAN_H
#define KORE_NATIVE_PLAN_H
#include "caiaq.h"
struct caiaq_native_plan {
    unsigned lengths[8];
    size_t payload_offsets[8]; /* chronological packed PCM locations */
    uint32_t transaction_offsets[8]; /* zero requests may point at buffer start */
    size_t payload_bytes, backing_bytes;
};
static inline int caiaq_native_plan(const unsigned lengths[8], int zero_at_start,
                                    unsigned guard_bytes, struct caiaq_native_plan *out) {
    struct caiaq_native_plan plan={0};
    if(!out || (zero_at_start!=0 && zero_at_start!=1) || guard_bytes>1 ||
       caiaq_layout(lengths,8,4096,plan.payload_offsets,&plan.payload_bytes)) return -1;
    if(!plan.payload_bytes) return -1; /* no native all-zero submission */
    plan.backing_bytes=plan.payload_bytes+guard_bytes;
    for(unsigned i=0;i<8;i++) {
        plan.lengths[i]=lengths[i];
        plan.transaction_offsets[i]=(uint32_t)((zero_at_start && !lengths[i])?0:plan.payload_offsets[i]);
    }
    *out=plan;return 0;
}
#endif
