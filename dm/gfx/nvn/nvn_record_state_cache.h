// INPUT: By-value idempotent state assignments in one command-buffer recording.
// OUTPUT: Omit only exact repeated assignments; calls, begin and generation changes invalidate.
// POS: Producer-side CPU optimization; draw/resource freshness and GPU execution are unchanged.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define NVN_RECORD_STATE_KEYS 128u
#define NVN_RECORD_STATE_BYTES 320u
typedef struct {
    uint64_t generation,checks,omitted,bytes;
    uint16_t size[NVN_RECORD_STATE_KEYS];
    uint8_t data[NVN_RECORD_STATE_KEYS][NVN_RECORD_STATE_BYTES];
} NvnRecordStateCache;
static inline void nvn_record_state_invalidate(NvnRecordStateCache *s){memset(s->size,0,sizeof s->size);}
static inline void nvn_record_state_prepare(NvnRecordStateCache *s,uint64_t generation){
    if(s->generation!=generation){nvn_record_state_invalidate(s);s->generation=generation;}
}
static inline int nvn_record_state_changed(NvnRecordStateCache *s,uint64_t generation,unsigned key,const void *value,size_t bytes){
    if(!s||!generation||key>=NVN_RECORD_STATE_KEYS||!value||!bytes||bytes>NVN_RECORD_STATE_BYTES)return 1;
    nvn_record_state_prepare(s,generation);
    if(s->size[key]==bytes&&!memcmp(s->data[key],value,bytes))return 0;
    memcpy(s->data[key],value,bytes);s->size[key]=(uint16_t)bytes;return 1;
}
