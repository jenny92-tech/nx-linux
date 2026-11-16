// INPUT: Ordered completed query references, counter resets/reports and pool reuse.
// OUTPUT: Exact sample sums; unread intervals discarded by reset need no host read.
// POS: Counter liveness across submissions. Unavailable observable results fail.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* At most two submission pools, each with 4096 query slots. A pool is
 * resolved before reuse, so references cannot grow without bound. */
enum { NVN_DEFERRED_COUNTER_LIMIT=8192 };
typedef int (*NvnCounterRead)(void *,uint64_t,uint32_t,uint64_t *);
typedef struct {
    uint64_t sum;unsigned n;
    struct {uint64_t pool;uint32_t query;} ref[NVN_DEFERRED_COUNTER_LIMIT];
} NvnDeferredCounter;
static inline void nvn_counter_deferred_reset(NvnDeferredCounter *s){s->sum=0;s->n=0;}
static inline int nvn_counter_deferred_resolve(NvnDeferredCounter *s,uint64_t pool,int all,
        NvnCounterRead read,void *user){
    unsigned kept=0,old=s->n;
    for(unsigned i=0;i<old;i++){
        if(!all&&s->ref[i].pool!=pool){s->ref[kept++]=s->ref[i];continue;}
        uint64_t value;
        if(!read||!read(user,s->ref[i].pool,s->ref[i].query,&value)){
            memmove(s->ref+kept,s->ref+i,(old-i)*sizeof *s->ref);s->n=kept+old-i;return 0;
        }
        s->sum+=value;
    }
    s->n=kept;return 1;
}
static inline int nvn_counter_deferred_add(NvnDeferredCounter *s,uint64_t pool,uint32_t query){
    if(s->n==NVN_DEFERRED_COUNTER_LIMIT)return 0;
    s->ref[s->n].pool=pool;s->ref[s->n].query=query;s->n++;return 1;
}
