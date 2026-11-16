// INPUT: Production span planner and independent byte-level ownership masks.
// OUTPUT: Exact dirty-copy coverage, preserved GPU bytes and bounded arithmetic.
// POS: Backend-neutral transfer policy regression, including Vulkan's adapter.
#include "../../gfx/nvn/nvn_vk_pool_upload.h"
#include <assert.h>
#include <stdio.h>
static uint8_t source[256],result[256],dirty_mask[256],visits[256];
static uint64_t base;static unsigned checks;static uint32_t random_state=17;
static uint32_t random_u32(void){random_state=random_state*1664525u+1013904223u;return random_state;}
static int next_dirty(const void *addr,size_t size,uint64_t stamp,size_t *cursor,size_t *off,size_t *len){
    (void)stamp;size_t start=(uint64_t)(uintptr_t)addr-base,at=*cursor;
    while(at<size&&!dirty_mask[start+at])at++;
    if(at==size)return 0;
    *off=at;while(at<size&&dirty_mask[start+at])at++;*len=at-*off;*cursor=at;return 1;
}
static void emit(void *context,uint64_t addr,uint64_t size){
    (void)context;assert(addr>=base&&addr+size<=base+sizeof source);
    for(uint64_t i=addr-base;i<addr-base+size;i++){result[i]=source[i];visits[i]++;}
}
static int broken(const void *p,size_t n,uint64_t s,size_t *c,size_t *o,size_t *l){(void)p;(void)n;(void)s;(void)c;*o=0;*l=1;return 1;}
int main(void){
    uint8_t copy_source[32]={1,2,3},copy_dest[32];memcpy(copy_dest,copy_source,sizeof copy_source);
    uint64_t from=(uint64_t)(uintptr_t)copy_source,to=(uint64_t)(uintptr_t)copy_dest;
    assert(nvn_transfer_cpu_copy_unchanged(from,to,32,NULL,0));
    NvnTransferRange writer={from+31,1};assert(!nvn_transfer_cpu_copy_unchanged(from,to,32,&writer,1));
    writer=(NvnTransferRange){to,1};assert(!nvn_transfer_cpu_copy_unchanged(from,to,32,&writer,1));
    writer=(NvnTransferRange){(from>to?from:to)+32,1};assert(nvn_transfer_cpu_copy_unchanged(from,to,32,&writer,1));
    copy_dest[31]=4;assert(!nvn_transfer_cpu_copy_unchanged(from,to,32,NULL,0));
    assert(!nvn_transfer_cpu_copy_unchanged(UINT64_MAX-1,to,32,NULL,0));
    assert(!nvn_transfer_cpu_copy_unchanged(from,to,0,NULL,0));
    assert(!nvn_transfer_cpu_copy_unchanged(from,to,1,NULL,1));
    base=(uint64_t)(uintptr_t)source;for(unsigned i=0;i<sizeof source;i++)source[i]=(uint8_t)(i*19+3);
    for(unsigned trial=0;trial<10000;trial++){
        unsigned offset=random_u32()%128,size=random_u32()%129,count=random_u32()%9;
        NvnTransferRange writes[8];uint8_t gpu[256]={0};
        for(unsigned i=0;i<256;i++)dirty_mask[i]=(uint8_t)(random_u32()%3==0);
        for(unsigned i=0;i<count;i++){
            unsigned first=random_u32()%256,length=random_u32()%(257-first);writes[i]=(NvnTransferRange){base+first,length};
            for(unsigned j=first;j<first+length;j++)gpu[j]=1;
        }
        for(unsigned partial=0;partial<2;partial++){
            memset(result,0xa5,sizeof result);memset(visits,0,sizeof visits);
            uint64_t copied=nvn_transfer_visit(base+offset,size,writes,count,1,partial?next_dirty:NULL,emit,NULL),expected=0;
            for(unsigned i=0;i<256;i++){
                int copy=i>=offset&&i<offset+size&&!gpu[i]&&(!partial||dirty_mask[i]);
                assert(visits[i]==copy);assert(result[i]==(copy?source[i]:0xa5));expected+=copy;checks++;
            }
            assert(copied==expected);checks++;
        }
        memset(result,0xa5,sizeof result);memset(visits,0,sizeof visits);
        uint64_t gpu_copied=nvn_transfer_visit_written(base+offset,size,writes,count,emit,NULL),gpu_expected=0;
        for(unsigned i=0;i<256;i++){
            int copy=i>=offset&&i<offset+size&&gpu[i];
            assert(visits[i]==copy);assert(result[i]==(copy?source[i]:0xa5));gpu_expected+=copy;checks++;
        }
        assert(gpu_copied==gpu_expected);checks++;
        int all=1;for(unsigned i=offset;i<offset+size;i++)if(!gpu[i])all=0;
        assert(nvn_vk_pool_fully_gpu_owned(base+offset,size,writes,count)==all);checks++;
        memset(result,0xa5,sizeof result);
        uint64_t copied=nvn_vk_pool_copy_guest_span(result,base,0,base+offset,size,writes,count),expected=0;
        for(unsigned i=0;i<256;i++){int copy=i>=offset&&i<offset+size&&!gpu[i];assert(result[i]==(copy?source[i]:0xa5));expected+=copy;checks++;}
        assert(copied==expected);checks++;
    }
    NvnTransferRange invalid={UINT64_MAX,2};
    assert(nvn_transfer_visit(base,16,&invalid,1,0,NULL,emit,NULL)==UINT64_MAX);
    assert(nvn_transfer_visit(UINT64_MAX-2,4,NULL,0,0,NULL,emit,NULL)==UINT64_MAX);
    assert(nvn_transfer_visit(base,16,NULL,0,0,broken,emit,NULL)==UINT64_MAX);
    assert(nvn_transfer_visit_written(base,16,&invalid,1,emit,NULL)==UINT64_MAX);
    assert(nvn_transfer_visit_written(UINT64_MAX-2,4,NULL,0,emit,NULL)==UINT64_MAX);
    printf("{\"checks\":%u,\"random_cases\":10000,\"gpu_preserved\":true,\"dirty_coverage\":true,\"vulkan_adapter\":true}\n",checks+3);
    return 0;
}
