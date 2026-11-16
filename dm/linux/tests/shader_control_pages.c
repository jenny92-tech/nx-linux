// INPUT: Adjacent private CPU watches, live mutations and mapping/pool replacement.
// OUTPUT: A cross-page header is clean only while both registration proofs hold.
// POS: Linux sanitizer regression for optional partial-page shader-control tracking.
#include "../../gfx/nvn/nvn_dirty.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
void crash_install(void);
void dm_guest_write_prepare(void *,size_t);
static uint64_t protect(void *p,size_t n,uint64_t tokens[2]){
    uint64_t stamp=nvn_dirty_now();assert(nvn_dirty_protect_control_pages(p,n,tokens));return stamp;
}
int main(void){
    crash_install();size_t pg=(size_t)sysconf(_SC_PAGESIZE),n=pg*8,bytes=2176;
    unsigned char *p=mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(p!=MAP_FAILED);
    memset(p,0x5a,n);nvn_dirty_add_range(p+pg*6,pg);
    unsigned char *a=p+pg-64,*b=p+pg*2-32;
    uint64_t first[2]={0},split[2]={0};assert(nvn_dirty_watch_control_pages(a,bytes,first));
    assert(first[0]&&!first[1]);uint64_t stamp=protect(a,bytes,first);
    assert(nvn_dirty_control_pages_clean(a,bytes,first,stamp));
    /* The second header extends past the first watch. Reuse its existing head
     * page and track the new tail independently, without overlapping ranges. */
    assert(nvn_dirty_watch_control_pages(b,bytes,split));assert(split[0]==first[0]&&split[1]&&split[1]!=first[0]);
    assert(!nvn_dirty_control_pages_clean(b,bytes,split,stamp));
    for(size_t i=0;i<bytes;i+=17){
        stamp=protect(b,bytes,split);assert(nvn_dirty_control_pages_clean(b,bytes,split,stamp));
        b[i]^=1;assert(!nvn_dirty_control_pages_clean(b,bytes,split,stamp));b[i]^=1;
    }
    stamp=protect(b,bytes,split);dm_guest_write_prepare(b+bytes-1,1);b[bytes-1]^=1;
    assert(!nvn_dirty_control_pages_clean(b,bytes,split,stamp));
    uint64_t old[2]={split[0],split[1]};stamp=protect(b,bytes,split);
    nvn_dirty_drop_control_watches(p+pg*2,pg);
    assert(!nvn_dirty_control_pages_clean(b,bytes,old,UINT64_MAX));
    assert(!nvn_dirty_protect_control_pages(b,bytes,old));
    assert(nvn_dirty_watch_control_pages(b,bytes,split));assert(split[0]==old[0]&&split[1]!=old[1]);
    stamp=protect(b,bytes,split);assert(nvn_dirty_control_pages_clean(b,bytes,split,stamp));
    assert(!nvn_dirty_control_pages_clean(b,bytes,old,UINT64_MAX));
    /* Taking over only the shared head page invalidates the whole header.
     * A normal NVN pool must never be promoted into a CPU-watch token. */
    nvn_dirty_add_range(p+pg,pg);assert(!nvn_dirty_control_pages_clean(b,bytes,split,UINT64_MAX));
    assert(!nvn_dirty_watch_control_pages(b,bytes,split));
    unsigned char *c=p+pg*4-32;uint64_t readonly[2]={0};
    assert(nvn_dirty_watch_control(p+pg*3,32,0));assert(!mprotect(p+pg*4,pg,PROT_READ));
    assert(!nvn_dirty_watch_control_pages(c,bytes,readonly));
    assert(!mprotect(p+pg*4,pg,PROT_READ|PROT_WRITE));
    assert(nvn_dirty_watch_control_pages(c,bytes,readonly));stamp=protect(c,bytes,readonly);
    nvn_dirty_control_mapping_begin(p+pg*4,pg);assert(!mprotect(p+pg*4,pg,PROT_READ));nvn_dirty_control_mapping_end();
    assert(!nvn_dirty_control_pages_clean(c,bytes,readonly,UINT64_MAX));
    assert(!nvn_dirty_protect_control_pages(c,bytes,readonly));
    assert(!nvn_dirty_watch_control_pages(c,bytes,readonly));
    assert(!mprotect(p+pg*4,pg,PROT_READ|PROT_WRITE));
    uint64_t empty[2]={0};assert(!nvn_dirty_watch_control_pages(NULL,bytes,empty));
    assert(!nvn_dirty_watch_control_pages((void *)(uintptr_t)(UINTPTR_MAX-4),bytes,empty));
    assert(!nvn_dirty_watch_control_pages(p,pg*3,empty));assert(!nvn_dirty_watch_control_pages(p,1,NULL));
    assert(!nvn_dirty_control_pages_clean(p,1,NULL,stamp));
    assert(!nvn_dirty_protect_control_pages(p,1,NULL));
    nvn_dirty_drop_control_watches(NULL,0);nvn_dirty_remove_range(p+pg);nvn_dirty_remove_range(p+pg*6);
    assert(!munmap(p,n));puts("control pages: both-page CPU/host mutation, stale tokens, partial pool/mapping takeover and readonly/overflow rejection passed");
}
