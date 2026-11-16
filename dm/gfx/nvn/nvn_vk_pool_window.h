// INPUT: Validated logical pool size, relative range and power-of-two granule.
// OUTPUT: A bounded aligned backing window containing the complete requested range.
// POS: Pure residency planner; never changes NVN addresses or allocation sizes.
#pragma once
#include <stdint.h>

static inline int nvn_vk_pool_window_bounds(uint64_t pool_size,uint64_t offset,uint64_t bytes,
        uint64_t granule,uint64_t *start,uint64_t *size) {
    if(!start||!size||!granule||(granule&(granule-1))||!bytes||offset>pool_size||bytes>pool_size-offset)return 0;
    uint64_t low=offset&~(granule-1),end=offset+bytes,high=end;
    uint64_t tail=end&(granule-1);
    if(tail){uint64_t padding=granule-tail;high=padding>pool_size-end?pool_size:end+padding;}
    *start=low;*size=high-low;return *size!=0;
}

static inline int nvn_vk_pool_window_contains(uint64_t start,uint64_t size,uint64_t offset,uint64_t bytes) {
    return offset>=start&&offset-start<=size&&bytes<=size-(offset-start);
}
