// INPUT:  Procfs memory counters and this process's own OOM priority.
// OUTPUT: A protected desktop, bounded allocation checks and exit-75 receipts.
// POS:    Linux test-process containment, independent of guest success/failure.
#include "../host/dm.h"
#include "memory_guard.h"
#include <stdatomic.h>
#include <malloc.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

static uint64_t reserve=512ull*1024*1024;
static uint64_t next_poll;
static atomic_flag exiting=ATOMIC_FLAG_INIT;
static int trim_enabled;
static _Atomic uint64_t last_trim;
static uint64_t memory_counter(const char *name) {
    FILE *input=fopen("/proc/meminfo","r");
    if(!input)die("Linux memory guard cannot read MemAvailable");
    char line[160],key[64],unit[8];unsigned long long kb=0;int found=0;
    while(fgets(line,sizeof line,input))if(sscanf(line,"%63s %llu %7s",key,&kb,unit)==3&&
        !strcmp(key,name)&&!strcmp(unit,"kB")){found=1;break;}
    fclose(input);
    if(!found||kb>UINT64_MAX/1024)die("Linux memory guard invalid MemAvailable");
    return (uint64_t)kb*1024;
}
static uint64_t available(void) { return memory_counter("MemAvailable:"); }
static uint64_t available_for(uint64_t bytes) {
    uint64_t free_bytes=available();
    /* Return only unused glibc pages. Guest mappings, live objects and the
     * desktop reserve are untouched. Serialize/rate-limit this optional
     * experiment across the frontend and renderer allocation paths. */
    if(trim_enabled&&(free_bytes<reserve||bytes>free_bytes-reserve||
        free_bytes-reserve-bytes<128ull*1024*1024)) {
        struct timespec now;
        if(clock_gettime(CLOCK_MONOTONIC,&now))die("Linux heap trim monotonic clock");
        uint64_t ns=(uint64_t)now.tv_sec*1000000000+(uint64_t)now.tv_nsec;
        uint64_t previous=atomic_load(&last_trim);
        if(ns>=previous&&ns-previous>=1000000000&&atomic_compare_exchange_strong(&last_trim,&previous,ns)) {
            int returned=malloc_trim(0);uint64_t before=free_bytes;free_bytes=available();
            fprintf(stderr,"LINUX_HEAP_TRIM returned=%d available_before=%llu available_after=%llu\n",
                returned,(unsigned long long)before,(unsigned long long)free_bytes);
        }
    }
    return free_bytes;
}
static void limit(uint64_t free_bytes,uint64_t requested,uint64_t live,unsigned kind) {
    if(atomic_flag_test_and_set(&exiting))for(;;)pause();
    fprintf(stderr,"LINUX_MEMORY_LIMIT available=%llu reserve=%llu requested=%llu gpu_ledger=%llu kind=%u exit=75\n",
        (unsigned long long)free_bytes,(unsigned long long)reserve,(unsigned long long)requested,(unsigned long long)live,kind);
    const char *path=getenv("DM_MEMORY_REPORT");
    if(path) {
        FILE *out=fopen(path,"w");
        if(out){fprintf(out,"{\"memory_guard_triggered\":true,\"exit\":75,\"available_bytes\":%llu,\"reserve_bytes\":%llu,"
            "\"requested_bytes\":%llu,\"gpu_ledger_bytes\":%llu,\"allocation_class\":%u,\"gameplay_verified\":false}\n",
            (unsigned long long)free_bytes,(unsigned long long)reserve,(unsigned long long)requested,(unsigned long long)live,kind);fclose(out);}
    }
    _exit(75);
}
void dm_linux_memory_guard_init(void) {
    uint64_t physical=memory_counter("MemTotal:");
    if(!physical)die("Linux memory guard invalid MemTotal");
    reserve=dm_linux_memory_reserve_for_total(physical);
    const char *trim=getenv("DM_LINUX_MALLOC_TRIM");trim_enabled=trim&&!strcmp(trim,"1");
    struct rlimit cores={0,0};
    if(setrlimit(RLIMIT_CORE,&cores))die("Linux memory guard cannot disable test core dumps");
    FILE *out=fopen("/proc/self/oom_score_adj","w");
    if(!out)die("Linux memory guard cannot set test-process OOM priority");
    int written=fprintf(out,"1000\n");int closed=fclose(out);
    if(written<0||closed)die("Linux memory guard OOM priority write failed");
    FILE *input=fopen("/proc/self/oom_score_adj","r");int score=-1;
    if(!input||fscanf(input,"%d",&score)!=1||score!=1000)die("Linux test-process OOM priority not applied");
    fclose(input);
    fprintf(stderr,"LINUX_MEMORY_GUARD reserve_bytes=%llu physical_bytes=%llu own_oom_score_adj=%d\n",
        (unsigned long long)reserve,(unsigned long long)physical,score);
    dm_linux_memory_guard_allocation(0,0,UINT32_MAX);
}
void dm_linux_memory_guard_allocation(uint64_t bytes,uint64_t live,unsigned kind) {
    uint64_t free_bytes=available_for(bytes);
    if(free_bytes<reserve||bytes>free_bytes-reserve)limit(free_bytes,bytes,live,kind);
}
int dm_linux_memory_guard_optional(uint64_t bytes) {
    uint64_t free_bytes=available_for(bytes),headroom=16ull*1024*1024;
#ifdef DM_LINUX_SOULKNIGHT
    /* The title capture streams one RGB row through zlib after its explicitly
     * budgeted readback allocation. Keep 4 MiB for that bounded scratch; the
     * system reserve and every mandatory allocation check are unchanged. */
    headroom=4ull*1024*1024;
#endif
    return free_bytes>=reserve&&bytes<=free_bytes-reserve&&
        headroom<=free_bytes-reserve-bytes;
}
void dm_linux_memory_guard_poll(void) {
    struct timespec now;
    if(clock_gettime(CLOCK_MONOTONIC,&now))die("Linux memory guard monotonic clock");
    uint64_t ns=(uint64_t)now.tv_sec*1000000000+(uint64_t)now.tv_nsec;
    if(ns<next_poll)return;
    next_poll=ns+20000000;
    dm_linux_memory_guard_allocation(0,0,UINT32_MAX);
}
