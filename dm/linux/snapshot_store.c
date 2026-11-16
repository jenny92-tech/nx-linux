// INPUT: Immutable baseline/shader bytes, an owned cache directory and optional RAM budget.
// OUTPUT: Exact owned bytes; small entries may retain bounded RAM, others use unlinked mappings.
// POS: Linux storage policy; explicit release, streaming correctness and no game/save changes.
#include "../host/dm.h"
#include "snapshot_store.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
typedef struct SnapshotRam {uint8_t *bytes;size_t size;struct SnapshotRam *next;} SnapshotRam;
static pthread_mutex_t snapshot_mu=PTHREAD_MUTEX_INITIALIZER;
static SnapshotRam *snapshot_ram;
static DmLinuxSnapshotStats snapshot_stats;
static int snapshot_configured;
static int snapshot_keep_ram(uint8_t *bytes,size_t size) {
    pthread_mutex_lock(&snapshot_mu);
    if(!snapshot_configured){
        const char *value=getenv("DM_LINUX_SNAPSHOT_RAM_MB");
        if(value){char *end;errno=0;unsigned long mb=strtoul(value,&end,10);
            if(errno||!*value||*end||mb>128)die("Linux snapshot RAM budget requires 0..128 MiB");
            snapshot_stats.limit_bytes=(uint64_t)mb*1024*1024;
        }
        snapshot_configured=1;
        if(snapshot_stats.limit_bytes)LOG("SNAPSHOT_RAM_CONFIG limit_bytes=%llu max_entry_bytes=65536 max_entries=8192",
            (unsigned long long)snapshot_stats.limit_bytes);
    }
    int keep=0;
    if(size<=65536&&snapshot_stats.resident_entries<8192&&size<=snapshot_stats.limit_bytes-snapshot_stats.resident_bytes){
        SnapshotRam *entry=malloc(sizeof *entry);
        if(entry){
            *entry=(SnapshotRam){bytes,size,snapshot_ram};snapshot_ram=entry;
            snapshot_stats.resident_bytes+=size;snapshot_stats.resident_entries++;snapshot_stats.ram_stores++;
            if(snapshot_stats.resident_bytes>snapshot_stats.peak_bytes)snapshot_stats.peak_bytes=snapshot_stats.resident_bytes;
            keep=1;
        }
    }
    pthread_mutex_unlock(&snapshot_mu);return keep;
}
void dm_linux_snapshot_stats(DmLinuxSnapshotStats *out) {
    pthread_mutex_lock(&snapshot_mu);*out=snapshot_stats;pthread_mutex_unlock(&snapshot_mu);
}
int dm_linux_snapshot_store(uint8_t **bytes,size_t size) {
    const char *root=getenv("DM_LINUX_SNAPSHOT_CACHE");
    if(!root)return 0;
    if(root[0]!='/'||!bytes||!*bytes||!size)die("Linux snapshot cache requires owned absolute directory and bytes");
    if(snapshot_keep_ram(*bytes,size))return 0;
    char path[1400];int length=snprintf(path,sizeof path,"%s/baseline-XXXXXX",root);
    if(length<0||length>=(int)sizeof path)die("Linux snapshot cache path too long");
    int fd=mkostemp(path,O_CLOEXEC);
    if(fd<0)die("Linux snapshot cache create errno=%d",errno);
    if(unlink(path))die("Linux snapshot cache unlink errno=%d",errno);
    if(ftruncate(fd,(off_t)size))die("Linux snapshot cache length errno=%d",errno);
    uint8_t *mapped=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(mapped==MAP_FAILED)die("Linux snapshot cache mapping errno=%d",errno);
    memcpy(mapped,*bytes,size);
    if(msync(mapped,size,MS_SYNC)||mprotect(mapped,size,PROT_READ))die("Linux snapshot cache flush/protection errno=%d",errno);
    if(close(fd))die("Linux snapshot cache close errno=%d",errno);
    free(*bytes);*bytes=mapped;
    if(madvise(mapped,size,MADV_DONTNEED))die("Linux snapshot cache reclaim hint errno=%d",errno);
    pthread_mutex_lock(&snapshot_mu);snapshot_stats.file_stores++;pthread_mutex_unlock(&snapshot_mu);
    return 1;
}
void dm_linux_snapshot_release(uint8_t *bytes,size_t size,int mapped) {
    if(mapped){if(munmap(bytes,size))die("Linux snapshot cache release errno=%d",errno);}
    else {
        pthread_mutex_lock(&snapshot_mu);
        SnapshotRam **at=&snapshot_ram;
        while(*at&&(*at)->bytes!=bytes)at=&(*at)->next;
        if(*at){SnapshotRam *entry=*at;
            if(entry->size!=size||snapshot_stats.resident_bytes<size||!snapshot_stats.resident_entries)
                die("Linux snapshot RAM ownership/size mismatch");
            *at=entry->next;snapshot_stats.resident_bytes-=size;snapshot_stats.resident_entries--;free(entry);
        }
        pthread_mutex_unlock(&snapshot_mu);free(bytes);
    }
}
