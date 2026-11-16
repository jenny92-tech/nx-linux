// INPUT: Boundary, overflow and overlapping-window requests without a GPU.
// OUTPUT: Verified complete containment and bounded physical window plans.
// POS: Residency arithmetic regression; no game or device dependency.
#include "nvn_vk_pool_window.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

static void verify_granule(uint64_t unit) {
    const uint64_t pool=320ULL*1024*1024;
    uint64_t low=0,size=0;
    assert(nvn_vk_pool_window_bounds(pool,0,64,unit,&low,&size)&&low==0&&size==unit);
    assert(nvn_vk_pool_window_bounds(pool,unit-4,8,unit,&low,&size)&&low==0&&size==2*unit);
    assert(nvn_vk_pool_window_bounds(pool,pool-1,1,unit,&low,&size)&&low==pool-unit&&size==unit);
    assert(nvn_vk_pool_window_bounds(pool-3,pool-4,1,unit,&low,&size)&&size==unit-3);
    assert(!nvn_vk_pool_window_bounds(pool,pool,1,unit,&low,&size));
    assert(!nvn_vk_pool_window_bounds(pool,0,0,unit,&low,&size));
    assert(!nvn_vk_pool_window_bounds(pool,0,1,0,&low,&size));
    assert(!nvn_vk_pool_window_bounds(pool,0,1,3,&low,&size));
    assert(!nvn_vk_pool_window_bounds(pool,0,1,unit,NULL,&size));
    assert(!nvn_vk_pool_window_bounds(pool,UINT64_MAX,1,unit,&low,&size));
    assert(nvn_vk_pool_window_bounds(UINT64_MAX,UINT64_MAX-4,4,unit,&low,&size));
    assert(size==UINT64_MAX-low);
    for(uint64_t offset=0;offset<pool;offset+=7919){
        uint64_t bytes=offset%65536+1;if(bytes>pool-offset)bytes=pool-offset;
        assert(nvn_vk_pool_window_bounds(pool,offset,bytes,unit,&low,&size));
        assert(low%unit==0&&size<=pool-low&&nvn_vk_pool_window_contains(low,size,offset,bytes));
        assert(size<=bytes+2*unit);
    }
    assert(!nvn_vk_pool_window_contains(8,4,7,1));
    assert(!nvn_vk_pool_window_contains(8,4,12,1));
}

int main(void) {
    const uint64_t granules[]={64ULL*1024,256ULL*1024,1024ULL*1024,4ULL*1024*1024};
    for(size_t i=0;i<sizeof granules/sizeof granules[0];i++)verify_granule(granules[i]);
    puts("pool window boundary and overflow checks passed for 64/256/1024/4096 KiB");return 0;
}
