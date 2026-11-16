// INPUT: Fully validated shader code, protected-page generation and current GPU write ranges.
// OUTPUT: Reuse only for the same immutable index entry, pool incarnation and recorder epoch.
// POS: Code-only identity lease; current control bytes must still be compared by the caller.
#pragma once
#include "nvn_vk_pool_upload.h"
typedef struct {
    const void *entry;uint64_t address,size,pool_serial,epoch,stamp;unsigned valid;
} NvnCodeLease;
static inline int nvn_code_lease_key(const NvnCodeLease *s,const void *entry,uint64_t address,
        uint64_t size,uint64_t pool_serial,uint64_t epoch){
    return s->valid&&s->entry==entry&&s->address==address&&s->size==size&&
        s->pool_serial==pool_serial&&s->epoch==epoch;
}
static inline int nvn_code_lease_no_gpu_writes(uint64_t address,uint64_t size,
        const PoolUploadRange *writes,size_t count){
    if(!address||!size||size>UINT64_MAX-address)return 0;
    for(size_t i=0;i<count;i++){
        PoolUploadRange w=writes[i];if(w.size>UINT64_MAX-w.addr)return 0;
        if(w.size&&address<w.addr+w.size&&w.addr<address+size)return 0;
    }
    return 1;
}
