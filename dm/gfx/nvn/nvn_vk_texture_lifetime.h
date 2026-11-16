/* Exact allocation identity and real-submission retirement. A completed
 * software ticket is not a GPU fence; callers clear epochs only after the
 * corresponding slot has completed GPU work AND guest publication. */
#pragma once
#include <stdint.h>
#include <string.h>
typedef struct {uint32_t flags,type,format,width,height,depth,levels,layers,samples,tiling,usage,sharing,view;} NvnTextureAllocationKey;
typedef struct {uint64_t epoch[3];} NvnTextureRetirement;
static inline int nvn_texture_allocation_equal(const NvnTextureAllocationKey *a,const NvnTextureAllocationKey *b) {
    return !memcmp(a,b,sizeof *a);
}
static inline int nvn_texture_retirement_done(NvnTextureRetirement *r,unsigned slot,uint64_t epoch) {
    if(slot>=3||!epoch||r->epoch[slot]!=epoch)return 0;
    r->epoch[slot]=0;return 1;
}
static inline int nvn_texture_retirement_ready(const NvnTextureRetirement *r) {
    return !(r->epoch[0]|r->epoch[1]|r->epoch[2]);
}
