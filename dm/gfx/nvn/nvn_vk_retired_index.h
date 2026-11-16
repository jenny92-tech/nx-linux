// INPUT: Exact size/usage keys for fence-retired buffers and swap-removal indices.
// OUTPUT: Matching free slots without a full scan; no allocation or resource ownership changes.
// POS: Optional CPU lookup index; recorder and GPU fence lifetimes remain external.
#pragma once
#include <stdint.h>
#include <string.h>
#ifndef NVN_RETIRED_CAPACITY
#define NVN_RETIRED_CAPACITY 512
#endif
#define NVN_RETIRED_BUCKETS 1024u
_Static_assert(NVN_RETIRED_CAPACITY>0&&NVN_RETIRED_CAPACITY<65535,"retired index capacity");
typedef struct {
    uint16_t head[NVN_RETIRED_BUCKETS],next[NVN_RETIRED_CAPACITY];
    uint64_t size[NVN_RETIRED_CAPACITY];uint32_t usage[NVN_RETIRED_CAPACITY];
    unsigned n;
} NvnRetiredIndex;
static inline unsigned nvn_retired_bucket(uint64_t size,uint32_t usage){
    uint64_t h=(size^(uint64_t)usage*0x9e3779b97f4a7c15ull)*0xbf58476d1ce4e5b9ull;
    return (unsigned)(h^(h>>32))&(NVN_RETIRED_BUCKETS-1);
}
static inline void nvn_retired_link(NvnRetiredIndex *s,unsigned i){
    unsigned b=nvn_retired_bucket(s->size[i],s->usage[i]);
    s->next[i]=s->head[b];s->head[b]=(uint16_t)(i+1);
}
static inline int nvn_retired_add(NvnRetiredIndex *s,unsigned i,uint64_t size,uint32_t usage){
    if(i!=s->n||i>=NVN_RETIRED_CAPACITY)return 0;
    s->size[i]=size;s->usage[i]=usage;s->n++;nvn_retired_link(s,i);return 1;
}
static inline int nvn_retired_unlink(NvnRetiredIndex *s,unsigned i){
    uint16_t *p=&s->head[nvn_retired_bucket(s->size[i],s->usage[i])];
    for(unsigned guard=0;*p&&guard<s->n;guard++){
        unsigned at=*p-1;if(at>=s->n)return 0;
        if(at==i){*p=s->next[at];return 1;}p=&s->next[at];
    }
    return 0;
}
static inline int nvn_retired_remove_move(NvnRetiredIndex *s,unsigned i){
    if(i>=s->n||!nvn_retired_unlink(s,i))return 0;
    unsigned last=s->n-1;
    if(i!=last){
        if(!nvn_retired_unlink(s,last))return 0;
        s->size[i]=s->size[last];s->usage[i]=s->usage[last];nvn_retired_link(s,i);
    }
    s->n--;s->next[last]=0;s->size[last]=0;s->usage[last]=0;return 1;
}
static inline unsigned nvn_retired_find(const NvnRetiredIndex *s,uint64_t size,uint32_t usage,uint64_t *steps){
    unsigned p=s->head[nvn_retired_bucket(size,usage)];
    for(unsigned guard=0;p&&guard<s->n;guard++){
        unsigned i=p-1;if(i>=s->n)return UINT32_MAX;
        if(steps)(*steps)++;
        if(s->size[i]==size&&s->usage[i]==usage)return i;p=s->next[i];
    }
    return UINT32_MAX;
}
