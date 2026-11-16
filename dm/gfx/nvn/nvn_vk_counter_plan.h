// INPUT: Complete flattened command order for one immutable submitted batch.
// OUTPUT: Whether each draw's sample count can be observed before a reset.
// POS: Counter liveness only; unknown cross-batch tails remain live.
#pragma once
#include "nvn_obj.h"
#include <stddef.h>

/* Only a complete validated batch may discard the previous interval before
 * reusing its query pool. Timestamp reports do not observe sample counts. */
static inline int nvn_counter_prefix_reset(const uint8_t *const *ops,size_t count){
    for(size_t i=0;i<count;i++){
        op_hdr h;memcpy(&h,ops[i],sizeof h);
        if(h.size<8||h.size%8)return 0;
        if(h.op==OP_RESET_COUNTER||h.op==OP_REPORT_COUNTER){
            if(h.size<(h.op==OP_RESET_COUNTER?16u:24u))return 0;
            uint64_t type;memcpy(&type,ops[i]+8,8);
            if(h.op==OP_RESET_COUNTER)return type==NVN_COUNTER_TYPE_SAMPLES_PASSED;
            if(type==NVN_COUNTER_TYPE_SAMPLES_PASSED)return 0;
            if(type!=NVN_COUNTER_TYPE_TIMESTAMP&&type!=NVN_COUNTER_TYPE_TIMESTAMP_TOP)return 0;
        }
    }
    return 0;
}

static inline int nvn_counter_plan(const uint8_t *const *ops,size_t count,uint8_t *live) {
    int needed=1; /* A later batch may report the unfinished counter interval. */
    for(size_t at=count;at;){
        size_t i=--at;op_hdr h;memcpy(&h,ops[i],sizeof h);
        if(h.size<8||h.size%8)return 0;
        if(h.op==OP_RESET_COUNTER||h.op==OP_REPORT_COUNTER){
            if(h.size<(h.op==OP_RESET_COUNTER?16u:24u))return 0;
            uint64_t type;memcpy(&type,ops[i]+8,8);
            if(h.op==OP_RESET_COUNTER){
                if(type!=NVN_COUNTER_TYPE_SAMPLES_PASSED)return 0;
                needed=0;
            }else if(type==NVN_COUNTER_TYPE_SAMPLES_PASSED)needed=1;
            else if(type!=NVN_COUNTER_TYPE_TIMESTAMP&&type!=NVN_COUNTER_TYPE_TIMESTAMP_TOP)return 0;
        }
        live[i]=(uint8_t)needed;
    }
    return 1;
}
