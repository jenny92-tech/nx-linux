// INPUT: An owned alias store and a fresh, unpublished small NVN pool.
// OUTPUT: Bounded private COW pages; live bytes survive overlapping commits.
// POS: Optional Soul Knight hot storage policy, without swap or system tuning.
#pragma once
#include "alias_backing.h"
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#define DM_ALIAS_POOL_CPU_NO_ACCESS 0x1u
#define DM_ALIAS_POOL_COMPRESSIBLE 0x80u
#define DM_LINUX_ALIAS_POOL_MAXIMUM (16ull << 20)
#define DM_LINUX_ALIAS_POOL_BUDGET_MAXIMUM (64ull << 20)

typedef struct {uintptr_t begin,end;} DmLinuxAliasInterval;
typedef struct {
    DmLinuxAliasInterval *p,*gaps;
    size_t n,capacity,bytes,budget;
    int (*headroom)(uint64_t);
} DmLinuxAliasPools;
typedef struct {int converted;const char *skip;size_t requested,charged,private_bytes;} DmLinuxAliasPoolResult;

/* Caller serializes this ledger with map/unmap and promotion. There can be at
 * most budget/page live fragments: every one owns at least one complete page.
 * Allocate that bound up front. Repeated middle decommits need no allocation
 * after hole punching and cannot overflow a fixed interval scratch array. */
static inline int dm_linux_alias_pools_init(DmLinuxAliasPools *p,size_t budget,int (*headroom)(uint64_t)) {
    *p=(DmLinuxAliasPools){0};p->headroom=headroom;
    if(!budget)return 0;
    long page=sysconf(_SC_PAGESIZE);
    if(page<4096||(page&(page-1))){errno=EINVAL;return -1;}
    if(budget>DM_LINUX_ALIAS_POOL_BUDGET_MAXIMUM)budget=DM_LINUX_ALIAS_POOL_BUDGET_MAXIMUM;
    budget-=budget%(size_t)page;
    if(!budget)return 0;
    size_t capacity=budget/(size_t)page;
    p->p=calloc(capacity,sizeof *p->p);p->gaps=calloc(capacity+1,sizeof *p->gaps);
    if(!p->p||!p->gaps){free(p->p);free(p->gaps);*p=(DmLinuxAliasPools){0};errno=ENOMEM;return -1;}
    p->capacity=capacity;p->budget=budget;return 0;
}
static inline void dm_linux_alias_pools_destroy(DmLinuxAliasPools *p) {
    free(p->p);free(p->gaps);*p=(DmLinuxAliasPools){0};
}
static inline size_t dm_linux_alias_pools_gaps(DmLinuxAliasPools *p,uintptr_t begin,uintptr_t end) {
    size_t count=0;uintptr_t cursor=begin;
    for(size_t i=0;i<p->n&&cursor<end;i++) {
        DmLinuxAliasInterval v=p->p[i];
        if(v.end<=cursor)continue;
        if(v.begin>=end)break;
        if(v.begin>cursor)p->gaps[count++]=(DmLinuxAliasInterval){cursor,v.begin};
        if(v.end>cursor)cursor=v.end;
    }
    if(cursor<end)p->gaps[count++]=(DmLinuxAliasInterval){cursor,end};
    return count;
}
static inline int dm_linux_alias_pools_insert(DmLinuxAliasPools *p,uintptr_t begin,uintptr_t end) {
    size_t first=0,last;
    while(first<p->n&&p->p[first].end<begin)first++;
    last=first;
    while(last<p->n&&p->p[last].begin<=end) {
        if(p->p[last].begin<begin)begin=p->p[last].begin;
        if(p->p[last].end>end)end=p->p[last].end;
        last++;
    }
    size_t count=p->n-(last-first)+1;if(count>p->capacity)return -1;
    memmove(p->p+first+1,p->p+last,(p->n-last)*sizeof *p->p);
    p->p[first]=(DmLinuxAliasInterval){begin,end};p->n=count;return 0;
}
static inline void dm_linux_alias_pools_remove(DmLinuxAliasPools *p,uintptr_t begin,uintptr_t end) {
    size_t first=0,last,freed=0;
    while(first<p->n&&p->p[first].end<=begin)first++;
    last=first;
    while(last<p->n&&p->p[last].begin<end) {
        uintptr_t lo=p->p[last].begin>begin?p->p[last].begin:begin;
        uintptr_t hi=p->p[last].end<end?p->p[last].end:end;
        freed+=hi-lo;last++;
    }
    if(first==last)return;
    DmLinuxAliasInterval left={p->p[first].begin,begin},right={end,p->p[last-1].end};
    size_t keep=(left.begin<left.end)+(right.begin<right.end);
    memmove(p->p+first+keep,p->p+last,(p->n-last)*sizeof *p->p);
    size_t at=first;
    if(left.begin<left.end)p->p[at++]=left;
    if(right.begin<right.end)p->p[at]=right;
    p->n=p->n-(last-first)+keep;p->bytes-=freed;
}

/* The address/offset/inode proof must cover every byte. Untracked private
 * mappings are never re-seeded from file: they may already contain COW data. */
static inline int dm_linux_alias_vma_backed(const DmLinuxAliasBacking *store,uintptr_t begin,uintptr_t end,int shared) {
    struct stat st;if(store->fd<0||fstat(store->fd,&st))return 0;
    FILE *file=fopen("/proc/self/maps","r");if(!file)return 0;
    char line[4096];uintptr_t cursor=begin;
    while(fgets(line,sizeof line,file)) {
        unsigned long long lo,hi,offset,inode;unsigned dev_major,dev_minor;char permission[8];
        if(sscanf(line,"%llx-%llx %7s %llx %x:%x %llu",&lo,&hi,permission,&offset,&dev_major,&dev_minor,&inode)!=7)continue;
        if(hi<=begin||lo>=end)continue;
        if(lo>cursor||permission[0]!='r'||permission[1]!='w'||(shared&&permission[3]!='s')||
           dev_major!=(unsigned)major(st.st_dev)||dev_minor!=(unsigned)minor(st.st_dev)||
           inode!=(unsigned long long)st.st_ino||lo<(uintptr_t)store->base||offset!=lo-(uintptr_t)store->base){cursor=0;break;}
        cursor=hi<end?(uintptr_t)hi:end;if(cursor==end)break;
    }
    fclose(file);return cursor==end;
}
static inline int dm_linux_alias_pools_map(DmLinuxAliasPools *p,DmLinuxAliasBacking *store,void *address,size_t size) {
    size_t offset;if(dm_linux_alias_backing_range(store,address,size,&offset))return -1;
    if(!p->n)return dm_linux_alias_backing_map(store,address,size);
    size_t count=dm_linux_alias_pools_gaps(p,(uintptr_t)address,(uintptr_t)address+size),total=0;
    for(size_t i=0;i<count;i++)total+=p->gaps[i].end-p->gaps[i].begin;
    if(total&&dm_linux_alias_backing_admit(store,total))return -1;
    /* Finish disk admission before replacing any VMA. */
    for(size_t i=0;i<count;i++)if(fallocate(store->fd,FALLOC_FL_KEEP_SIZE,
        (off_t)(p->gaps[i].begin-(uintptr_t)store->base),(off_t)(p->gaps[i].end-p->gaps[i].begin)))return -1;
    for(size_t i=0;i<count;i++) {
        DmLinuxAliasInterval v=p->gaps[i];
        if(mmap((void *)v.begin,v.end-v.begin,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_FIXED,
                store->fd,(off_t)(v.begin-(uintptr_t)store->base))==MAP_FAILED) {
            /* A split fixed-map failure may have changed earlier VMAs. Do not
             * tell the guest this was an untouched, retryable allocation. */
            errno=EIO;return -1;
        }
    }
    /* A successful physical re-commit also restores writable permissions on
     * retained COW pages, just as replacing the original shared VMA did. */
    if(mprotect(address,size,PROT_READ|PROT_WRITE)){errno=EIO;return -1;}
    return 0;
}
static inline int dm_linux_alias_pools_unmap(DmLinuxAliasPools *p,DmLinuxAliasBacking *store,void *address,size_t size) {
    if(dm_linux_alias_backing_unmap(store,address,size))return -1;
    if(p->n)dm_linux_alias_pools_remove(p,(uintptr_t)address,(uintptr_t)address+size);
    return 0;
}
static inline int dm_linux_alias_pools_restore(DmLinuxAliasPools *p,DmLinuxAliasBacking *store,size_t count) {
    int failed=0;
    for(size_t i=0;i<count;i++) {
        DmLinuxAliasInterval v=p->gaps[i];
        if(mmap((void *)v.begin,v.end-v.begin,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_FIXED,
                store->fd,(off_t)(v.begin-(uintptr_t)store->base))==MAP_FAILED)failed=1;
    }
    if(failed){errno=EIO;return -1;}return 0;
}
/* Only the NVN initialization caller can establish freshness. There must be
 * no live overlapping pool or published CPU/GPU consumer. Returns -1 only if
 * failed mapping restoration makes continuing the process unsafe. */
static inline int dm_linux_alias_pools_promote(DmLinuxAliasPools *p,DmLinuxAliasBacking *store,
        const void *mem,size_t size,uint32_t flags,DmLinuxAliasPoolResult *result) {
    *result=(DmLinuxAliasPoolResult){0,"policy_off",size,0,p->bytes};
    if(!p->budget)return 0;
    if(store->fd<0){result->skip="no_backing";return 0;}
    if(flags&DM_ALIAS_POOL_CPU_NO_ACCESS){result->skip="cpu_no_access";return 0;}
    if(flags&DM_ALIAS_POOL_COMPRESSIBLE){result->skip="compressible";return 0;}
    if(size>DM_LINUX_ALIAS_POOL_MAXIMUM){result->skip="too_large";return 0;}
    size_t offset;
    if(dm_linux_alias_backing_range(store,(void *)mem,size,&offset)){result->skip="outside_or_unaligned";return 0;}
    uintptr_t begin=(uintptr_t)mem,end=begin+size;
    size_t count=dm_linux_alias_pools_gaps(p,begin,end),charged=0;
    for(size_t i=0;i<count;i++)charged+=p->gaps[i].end-p->gaps[i].begin;
    if(!charged){result->skip="already_private";return 0;}
    if(charged>p->budget-p->bytes){result->skip="over_budget";return 0;}
    if(p->n+1>p->capacity){result->skip="metadata_capacity";return 0;}
    /* Clean COW pages may not have consumed RAM yet. Conservatively admit
     * the whole outstanding private promise, not just this additional pool. */
    if(p->headroom&&!p->headroom(p->bytes+charged)){result->skip="ram_guard";return 0;}
    if(!dm_linux_alias_vma_backed(store,begin,end,0)){result->skip="not_committed";return 0;}
    for(size_t i=0;i<count;i++)if(!dm_linux_alias_vma_backed(store,p->gaps[i].begin,p->gaps[i].end,1)){
        result->skip="untracked_private";return 0;
    }
    for(size_t i=0;i<count;i++) {
        DmLinuxAliasInterval v=p->gaps[i];
        if(mmap((void *)v.begin,v.end-v.begin,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_FIXED,
                store->fd,(off_t)(v.begin-(uintptr_t)store->base))==MAP_FAILED) {
            if(dm_linux_alias_pools_restore(p,store,i+1))return -1;
            result->skip="private_map_failed";return 0;
        }
    }
    if(dm_linux_alias_pools_insert(p,begin,end)) {
        if(dm_linux_alias_pools_restore(p,store,count))return -1;
        result->skip="metadata_capacity";return 0;
    }
    p->bytes+=charged;*result=(DmLinuxAliasPoolResult){1,"",size,charged,p->bytes};return 1;
}
