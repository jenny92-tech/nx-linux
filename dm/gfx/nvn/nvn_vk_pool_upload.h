/* Copy guest ranges into the Vulkan pool mirror without overwriting bytes
 * owned by GPU writes in the current submission. */
#ifndef NVN_VK_POOL_UPLOAD_H
#define NVN_VK_POOL_UPLOAD_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "nvn_transfer.h"

typedef NvnTransferRange PoolUploadRange;

/* Callers supply validated pool ranges. Cover the whole union, including
 * unordered, adjacent and overlapping writes; a single uncovered byte fails. */
static inline int nvn_vk_pool_fully_gpu_owned(uint64_t addr,uint64_t len,
        const PoolUploadRange *writes,size_t write_count) {
    if(len>UINT64_MAX-addr)return 0;
    uint64_t cursor=addr,end=addr+len;
    while(cursor<end){uint64_t next=cursor;
        for(size_t i=0;i<write_count;i++){
            PoolUploadRange w=writes[i];
            if(w.size>UINT64_MAX-w.addr)return 0;
            if(w.addr<=cursor&&w.addr+w.size>next)next=w.addr+w.size;
        }
        if(next==cursor)return 0;
        cursor=next<end?next:end;
    }
    return 1;
}

typedef struct {uint8_t *mapped;uint64_t base,offset;} NvnVkPoolCopy;
static inline void nvn_vk_pool_emit_copy(void *context,uint64_t addr,uint64_t size){
    NvnVkPoolCopy *copy=context;
    memcpy(copy->mapped+copy->offset+(addr-copy->base),(const void *)(uintptr_t)addr,(size_t)size);
}
static inline uint64_t nvn_vk_pool_copy_guest_span(uint8_t *mapped,
        uint64_t base_addr,uint64_t map_offset,uint64_t addr,uint64_t len,
        const PoolUploadRange *writes,size_t write_count) {
    NvnVkPoolCopy copy={mapped,base_addr,map_offset};
    return nvn_transfer_visit(addr,len,writes,write_count,0,NULL,nvn_vk_pool_emit_copy,&copy);
}

#endif
