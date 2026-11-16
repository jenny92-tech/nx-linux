// INPUT: A 1 MiB budget, an owned temporary cache directory and synthetic buffers.
// OUTPUT: Exact fallback/reclaim bytes, reusable budget and concurrent ownership checks.
// POS: Standalone storage regression; no game, input, GPU or clock configuration.
#include "../snapshot_store.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
static uint8_t *buffer(size_t size,unsigned seed) {
    uint8_t *p=malloc(size);assert(p);
    for(size_t i=0;i<size;i++)p[i]=(uint8_t)((i*71u)^(i>>8)^seed);return p;
}
static void check(const uint8_t *p,size_t size,unsigned seed) {
    for(size_t i=0;i<size;i++)assert(p[i]==(uint8_t)((i*71u)^(i>>8)^seed));
}
static void *worker(void *opaque) {
    unsigned seed=(unsigned)(uintptr_t)opaque;
    for(unsigned i=0;i<100;i++){
        uint8_t *p=buffer(1537,seed);int mapped=dm_linux_snapshot_store(&p,1537);
        if(mapped)assert(!madvise(p,1537,MADV_DONTNEED));check(p,1537,seed);
        dm_linux_snapshot_release(p,1537,mapped);
    }
    return NULL;
}
int main(void) {
    uint8_t *p[16];for(unsigned i=0;i<16;i++){p[i]=buffer(65536,i);assert(!dm_linux_snapshot_store(&p[i],65536));}
    DmLinuxSnapshotStats stats;dm_linux_snapshot_stats(&stats);
    assert(stats.limit_bytes==1048576&&stats.resident_bytes==stats.limit_bytes&&stats.peak_bytes==stats.limit_bytes);
    uint8_t *extra=buffer(32769,99);assert(dm_linux_snapshot_store(&extra,32769)==1);
    assert(!madvise(extra,32769,MADV_DONTNEED));check(extra,32769,99);dm_linux_snapshot_release(extra,32769,1);
    check(p[0],65536,0);dm_linux_snapshot_release(p[0],65536,0);
    p[0]=buffer(65536,45);assert(!dm_linux_snapshot_store(&p[0],65536));check(p[0],65536,45);
    for(unsigned i=0;i<16;i++)dm_linux_snapshot_release(p[i],65536,0);
    extra=buffer(65537,19);assert(dm_linux_snapshot_store(&extra,65537)==1);check(extra,65537,19);dm_linux_snapshot_release(extra,65537,1);
    pthread_t threads[4];for(unsigned i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)(i+1)));
    for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
    /* Unregistered malloc storage retains the pre-existing release contract. */
    dm_linux_snapshot_release(buffer(33,9),33,0);dm_linux_snapshot_release(NULL,0,0);
    dm_linux_snapshot_stats(&stats);assert(!stats.resident_bytes&&!stats.resident_entries&&stats.peak_bytes<=stats.limit_bytes&&stats.file_stores==2);
    puts("snapshot RAM: cap, exact file fallback, budget reuse and concurrent release passed");return 0;
}
