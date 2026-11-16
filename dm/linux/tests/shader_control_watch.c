// INPUT: Real private/shared/readonly mappings, pool replacement and control mutations.
// OUTPUT: Only eligible CPU pages can retain an identity; every mutation/replacement invalidates it.
// POS: Linux sanitizer regression using the production dirty tracker and host write hook.
#include "../../gfx/nvn/nvn_dirty.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include <unistd.h>
void crash_install(void);
void dm_guest_write_prepare(void *,size_t);
static uint64_t protect(void *p,size_t n){uint64_t s=nvn_dirty_now();nvn_dirty_protect(p,n);return s;}
static void *pending_control;static size_t pending_bytes;static uint64_t pending_hint,pending_token;
static _Atomic int started;
static void *watch_during_mapping(void *unused){
    (void)unused;atomic_store(&started,1);
    pending_token=nvn_dirty_watch_control(pending_control,pending_bytes,pending_hint);return NULL;
}
int main(void){
    crash_install();size_t pg=(size_t)sysconf(_SC_PAGESIZE),n=pg*8;
    unsigned char *p=mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(p!=MAP_FAILED);
    memset(p,0x59,n);nvn_dirty_add_range(p+pg*6,pg);
    assert(!nvn_dirty_watch_control(p+pg*6,128,0));
    unsigned char *control=p+pg-64;size_t bytes=2176;
    uint64_t token=nvn_dirty_watch_control(control,bytes,0);assert(token);
    assert(!nvn_dirty_control_clean(control,bytes,token,nvn_dirty_now()));
    assert(nvn_dirty_watch_control(control,bytes,token)==token);
    /* Partial overlap cannot create a second range over existing protections. */
    assert(!nvn_dirty_watch_control(p+pg*2-32,256,0));
    assert(!nvn_dirty_watch_control(p,pg*3,0));
    for(size_t i=0;i<bytes;i+=71){
        uint64_t stamp=protect(control,bytes);assert(!nvn_dirty_since(control,bytes,stamp));
        assert(nvn_dirty_control_clean(control,bytes,token,stamp));
        control[i]^=1;assert(nvn_dirty_since(control,bytes,stamp));control[i]^=1;
        assert(!nvn_dirty_control_clean(control,bytes,token,stamp));
    }
    uint64_t stamp=protect(control,bytes);dm_guest_write_prepare(control+bytes-1,1);control[bytes-1]^=1;
    assert(nvn_dirty_since(control,bytes,stamp));
    assert(!nvn_dirty_control_clean(control,bytes,token,stamp));
    /* Reusing the same address never reuses its registration token. */
    nvn_dirty_drop_control_watches(control,bytes);assert(!nvn_dirty_tracked(control,bytes));
    assert(!nvn_dirty_control_clean(control,bytes,token,stamp));
    assert(nvn_dirty_since(control,bytes,stamp));
    uint64_t next=nvn_dirty_watch_control(control,bytes,token);assert(next&&next!=token);
    stamp=protect(control,bytes);assert(nvn_dirty_control_clean(control,bytes,next,stamp));
    assert(!nvn_dirty_control_clean(control,bytes,token,stamp));
    /* Pool registration retires overlapping CPU watches before taking ownership. */
    protect(control,bytes);nvn_dirty_add_range(p,pg*2);
    assert(!nvn_dirty_control_clean(control,bytes,next,UINT64_MAX));
    assert(!nvn_dirty_watch_control(control,bytes,next));
    nvn_dirty_remove_range(p);assert(!nvn_dirty_tracked(control,bytes));
    next=nvn_dirty_watch_control(control,bytes,next);assert(next&&next!=token);
    protect(control,bytes);nvn_dirty_drop_control_watches(p,n);
    assert(!mprotect(p,pg*2,PROT_NONE));assert(!mprotect(p,pg*2,PROT_READ));
    assert(!nvn_dirty_watch_control(control,bytes,next));
    assert(!mprotect(p,pg*2,PROT_READ|PROT_WRITE));
    token=nvn_dirty_watch_control(control,bytes,next);assert(token&&token!=next);
    /* A competing lookup cannot reinstall a watch between retiring it and
     * the kernel changing its mapping to read-only. */
    nvn_dirty_control_mapping_begin(control,bytes);
    pending_control=control;pending_bytes=bytes;pending_hint=token;pthread_t worker;
    assert(!pthread_create(&worker,NULL,watch_during_mapping,NULL));
    while(!atomic_load(&started)){};
    assert(!mprotect(p,pg*2,PROT_READ));nvn_dirty_control_mapping_end();
    assert(!pthread_join(worker,NULL));assert(!pending_token);
    assert(!mprotect(p,pg*2,PROT_READ|PROT_WRITE));
    next=nvn_dirty_watch_control(control,bytes,token);assert(next&&next!=token);
    stamp=protect(control,bytes);assert(nvn_dirty_control_clean(control,bytes,next,stamp));
    assert(!nvn_dirty_control_clean(control,bytes,token,UINT64_MAX));
    assert(!nvn_dirty_control_clean(control,bytes,0,stamp));
    assert(!nvn_dirty_control_clean(control,0,next,stamp));
    unsigned char *shared=mmap(NULL,pg,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);assert(shared!=MAP_FAILED);
    assert(!nvn_dirty_watch_control(shared,128,0));
    assert(!nvn_dirty_watch_control((void *)(uintptr_t)(UINTPTR_MAX-5),128,0));
    assert(!nvn_dirty_watch_control(NULL,128,0));
    protect(control,bytes);nvn_dirty_drop_control_watches(NULL,0);control[0]^=1;
    assert(!nvn_dirty_tracked(control,bytes));assert(nvn_dirty_tracked(p+pg*6,pg));
    nvn_dirty_remove_range(p+pg*6);assert(!munmap(shared,pg));assert(!munmap(p,n));
    puts("control watch: CPU/host writes, address reuse, pool takeover, mapping changes, readonly/shared/overlap/overflow rejection passed");
}
