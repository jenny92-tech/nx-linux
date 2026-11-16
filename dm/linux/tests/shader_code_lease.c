// INPUT: Real protected Linux pages, CPU faults, host writes, GPU overlaps and pool/epoch reuse.
// OUTPUT: Every mutation invalidates a code lease; unchanged bytes retain their exact identity.
// POS: Host test with the production dirty tracker; no game files or GPU required.
#include "../../gfx/nvn/nvn_vk_code_lease.h"
#include "../../gfx/nvn/nvn_dirty.h"
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
void crash_install(void);
void dm_guest_write_prepare(void *,size_t);
static uint64_t protect(const void *p,size_t n){uint64_t s=nvn_dirty_now();nvn_dirty_protect(p,n);return s;}
int main(void){
    crash_install();size_t page=(size_t)sysconf(_SC_PAGESIZE),bytes=page*3;
    uint8_t *p=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(p!=MAP_FAILED);
    memset(p,0x62,bytes);nvn_dirty_add_range(p,bytes);
    uint8_t *code=p+17;size_t n=page*2+21;
    uint64_t stamp=protect(code,n);assert(nvn_dirty_tracked(code,n)&&!nvn_dirty_since(code,n,stamp));
    NvnCodeLease s={.entry=&s,.address=(uint64_t)(uintptr_t)code,.size=n,.pool_serial=4,.epoch=9,.stamp=stamp,.valid=1};
    assert(nvn_code_lease_key(&s,&s,(uint64_t)(uintptr_t)code,n,4,9));
    assert(!nvn_code_lease_key(&s,&s,(uint64_t)(uintptr_t)code,n,5,9));
    assert(!nvn_code_lease_key(&s,&s,(uint64_t)(uintptr_t)code,n,4,10));
    assert(!nvn_code_lease_key(&s,p,(uint64_t)(uintptr_t)code,n,4,9));
    for(size_t i=0;i<n;i+=127){
        stamp=protect(code,n);assert(!nvn_dirty_since(code,n,stamp));
        code[i]^=1;assert(nvn_dirty_since(code,n,stamp));code[i]^=1;
    }
    stamp=protect(code,n);dm_guest_write_prepare(code+n-1,1);code[n-1]^=1;
    assert(nvn_dirty_since(code,n,stamp));
    PoolUploadRange w={(uint64_t)(uintptr_t)code-16,16};
    assert(nvn_code_lease_no_gpu_writes(s.address,s.size,&w,1));w.size++;
    assert(!nvn_code_lease_no_gpu_writes(s.address,s.size,&w,1));
    w=(PoolUploadRange){s.address+s.size,8};assert(nvn_code_lease_no_gpu_writes(s.address,s.size,&w,1));
    w.addr--;assert(!nvn_code_lease_no_gpu_writes(s.address,s.size,&w,1));
    w=(PoolUploadRange){UINT64_MAX,2};assert(!nvn_code_lease_no_gpu_writes(s.address,s.size,&w,1));
    nvn_dirty_remove_range(p);assert(!nvn_dirty_tracked(code,n)&&nvn_dirty_since(code,n,stamp));
    nvn_dirty_add_range(p,bytes);assert(!nvn_code_lease_key(&s,&s,s.address,n,6,9));
    nvn_dirty_remove_range(p);assert(!munmap(p,bytes));
    puts("shader code lease: CPU faults, host prep, GPU/overflow overlap, pool reuse and epoch changes passed");
}
