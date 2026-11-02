/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../audio/native_plan.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    for(unsigned mask=1;mask<256;mask++)for(int start=0;start<=1;start++)for(unsigned guard=0;guard<=1;guard++) {
        unsigned lengths[8];for(unsigned i=0;i<8;i++)lengths[i]=(mask&(1u<<i))?176:0;
        struct caiaq_native_plan plan;
        assert(!caiaq_native_plan(lengths,start,guard,&plan));
        size_t offset=0;unsigned char payload[4098];memset(payload,0xa5,sizeof(payload));
        for(unsigned i=0;i<8;i++) {
            assert(plan.lengths[i]==lengths[i]);
            assert(plan.payload_offsets[i]==offset);
            assert(plan.transaction_offsets[i]==((start && !lengths[i])?0:offset));
            assert(!caiaq_pack(payload+plan.payload_offsets[i],plan.backing_bytes-plan.payload_offsets[i],lengths[i],NULL,NULL));
            offset+=lengths[i];
        }
        assert(plan.payload_bytes==offset && plan.backing_bytes==offset+guard);
        /* Guard storage is never part of any requested transaction. */
        assert(payload[offset]==0xa5 && payload[offset+guard]==0xa5);
    }
    unsigned lengths[8]={0};struct caiaq_native_plan plan;
    assert(caiaq_native_plan(lengths,1,1,&plan)==-1);
    lengths[0]=513;assert(caiaq_native_plan(lengths,1,1,&plan)==-1);
    lengths[0]=176;assert(caiaq_native_plan(lengths,1,2,&plan)==-1);
    assert(caiaq_native_plan(lengths,2,0,&plan)==-1);
    puts("PASS native plan: 1020 layouts, exact request order/count/lengths, explicit zero offsets, untransmitted guard, bounds");
}
