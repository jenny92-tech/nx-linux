// INPUT: Owned cache directory and the guest's already reserved alias region.
// OUTPUT: Process-local, unlinked file backing for committed physical ranges.
// POS: Opt-in low-RAM storage policy; no system swap or persistent settings.
#pragma once
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/statvfs.h>
#include <unistd.h>

typedef struct {size_t offset,size;} DmLinuxAliasDead;
typedef struct {int fd;uint8_t *base;size_t size,page;
    /* Dead ranges only. Retained file blocks and metadata are both bounded;
     * no pointer is shared by copied test stores or private-COW tracking. */
    size_t defer_limit,deferred_bytes,deferred_count;
    uint64_t deferred_unmaps,inline_zero_bytes,synchronous_unmaps;
    DmLinuxAliasDead dead[256];
} DmLinuxAliasBacking;

static inline int dm_linux_alias_backing_configure_defer(DmLinuxAliasBacking *s,size_t limit) {
    if(s->fd<0||!s->page||limit>64u*1024*1024||limit%s->page||s->deferred_count){errno=EINVAL;return -1;}
    s->defer_limit=limit;return 0;
}
static inline void dm_linux_alias_dead_remove(DmLinuxAliasBacking *s,size_t lo,size_t size) {
    size_t hi=lo+size;
    for(size_t i=0;i<s->deferred_count;){DmLinuxAliasDead r=s->dead[i];size_t end=r.offset+r.size;
        if(hi<=r.offset||lo>=end){i++;continue;}
        size_t a=lo>r.offset?lo:r.offset,b=hi<end?hi:end;s->deferred_bytes-=b-a;
        if(a>r.offset&&b<end){
            /* Callers flush before count reaches 255, reserving a split slot. */
            memmove(s->dead+i+2,s->dead+i+1,(s->deferred_count-i-1)*sizeof *s->dead);
            s->dead[i]=(DmLinuxAliasDead){r.offset,a-r.offset};s->dead[i+1]=(DmLinuxAliasDead){b,end-b};s->deferred_count++;i+=2;
        }else if(a>r.offset){s->dead[i].size=a-r.offset;i++;}
        else if(b<end){s->dead[i]=(DmLinuxAliasDead){b,end-b};i++;}
        else{memmove(s->dead+i,s->dead+i+1,(--s->deferred_count-i)*sizeof *s->dead);}
    }
}
static inline int dm_linux_alias_dead_flush(DmLinuxAliasBacking *s) {
    while(s->deferred_count){DmLinuxAliasDead r=s->dead[s->deferred_count-1];
        if(fallocate(s->fd,FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE,(off_t)r.offset,(off_t)r.size))return -1;
        s->deferred_count--;s->deferred_bytes-=r.size;
    }
    return 0;
}

static inline int dm_linux_alias_backing_init(DmLinuxAliasBacking *store,const char *root,void *base,size_t size) {
    long page=sysconf(_SC_PAGESIZE);
    if(!root||root[0]!='/'||!base||!size||size>INT64_MAX||page<=0||
       (uintptr_t)base%(size_t)page||size%(size_t)page){errno=EINVAL;return -1;}
    char path[1400];int length=snprintf(path,sizeof path,"%s/guest-memory-XXXXXX",root);
    if(length<0||(size_t)length>=sizeof path){errno=ENAMETOOLONG;return -1;}
    int fd=mkostemp(path,O_CLOEXEC);if(fd<0)return -1;
    if(unlink(path)){int error=errno;close(fd);errno=error;return -1;}
    struct statvfs fs;
    if(fstatvfs(fd,&fs)){int error=errno;close(fd);errno=error;return -1;}
    if(!fs.f_frsize||fs.f_frsize>(unsigned long)page){close(fd);errno=EOPNOTSUPP;return -1;}
    /* Sparse capacity reserves no RAM or data extents. Each commit below
     * reserves its own disk blocks before exposing writable guest pages. */
    if(ftruncate(fd,(off_t)size)){int error=errno;close(fd);errno=error;return -1;}
    /* Discover unsupported decommit before exposing any file-backed pages. */
    if(fallocate(fd,FALLOC_FL_KEEP_SIZE,0,page)||
       fallocate(fd,FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE,0,page)){
        int error=errno;close(fd);errno=error;return -1;
    }
    *store=(DmLinuxAliasBacking){.fd=fd,.base=base,.size=size,.page=(size_t)page};return 0;
}

static inline int dm_linux_alias_backing_range(const DmLinuxAliasBacking *store,void *address,size_t size,size_t *offset) {
    uintptr_t a=(uintptr_t)address,b=(uintptr_t)store->base;
    if(store->fd<0||a<b||!size||size>store->size||a-b>store->size-size||
       a%store->page||size%store->page){errno=EINVAL;return -1;}
    *offset=(size_t)(a-b);return 0;
}

/* Storage admission shared by a whole commit and its split private gaps: the
 * same 128 MiB filesystem reserve applies to every committed byte. */
static inline int dm_linux_alias_backing_admit(const DmLinuxAliasBacking *store,size_t size) {
    if(store->fd<0||!size){errno=EINVAL;return -1;}
    struct statvfs fs;if(fstatvfs(store->fd,&fs))return -1;
    uint64_t unit=fs.f_frsize?fs.f_frsize:fs.f_bsize;
    uint64_t free_bytes=(uint64_t)fs.f_bavail*unit,reserve=128ull*1024*1024;
    if(free_bytes<reserve||size>free_bytes-reserve){errno=ENOSPC;return -1;}
    return 0;
}

static inline int dm_linux_alias_backing_map(DmLinuxAliasBacking *store,void *address,size_t size) {
    size_t offset;if(dm_linux_alias_backing_range(store,address,size,&offset))return -1;
    if(store->deferred_count>=255&&dm_linux_alias_dead_flush(store))return -1;
    if(dm_linux_alias_backing_admit(store,size)){
        /* Retained dead blocks must not cause an avoidable ENOSPC that the
         * original synchronous unmap would have prevented. */
        if(!store->deferred_count||dm_linux_alias_dead_flush(store)||dm_linux_alias_backing_admit(store,size))return -1;
    }
    /* Preallocation prevents later page faults from failing due to ENOSPC.
     * The file is unlinked and only this process can truncate/punch it. */
    if(fallocate(store->fd,FALLOC_FL_KEEP_SIZE,(off_t)offset,(off_t)size))return -1;
    int needs_zero=0;
    for(size_t i=0;i<store->deferred_count;i++)if(store->dead[i].offset<offset+size&&offset<store->dead[i].offset+store->dead[i].size){needs_zero=1;break;}
    if(needs_zero){
        /* Zero through a temporary view of the SAME file before exposing the
         * primary guest mapping. Live neighbours/aliases retain their bytes. */
        uint8_t *zero=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_SHARED,store->fd,(off_t)offset);
        if(zero==MAP_FAILED)return -1;
        for(size_t i=0;i<store->deferred_count;i++){
            DmLinuxAliasDead r=store->dead[i];size_t a=r.offset>offset?r.offset:offset,b=r.offset+r.size<offset+size?r.offset+r.size:offset+size;
            if(a<b){memset(zero+a-offset,0,b-a);store->inline_zero_bytes+=b-a;}
        }
        if(munmap(zero,size))return -1;
    }
    if(mmap(address,size,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_FIXED,store->fd,(off_t)offset)==MAP_FAILED)return -1;
    dm_linux_alias_dead_remove(store,offset,size);return 0;
}

static inline int dm_linux_alias_backing_unmap(DmLinuxAliasBacking *store,void *address,size_t size) {
    size_t offset;if(dm_linux_alias_backing_range(store,address,size,&offset))return -1;
    if(store->deferred_count>=255&&dm_linux_alias_dead_flush(store))return -1;
    if(store->defer_limit){
        int mode=fcntl(store->fd,F_GETFL);if(mode<0)return -1;
        if((mode&O_ACCMODE)!=O_RDWR){errno=EACCES;return -1;}
        size_t extra=size,end=offset+size;
        for(size_t i=0;i<store->deferred_count;i++){DmLinuxAliasDead r=store->dead[i];
            size_t a=r.offset>offset?r.offset:offset,b=r.offset+r.size<end?r.offset+r.size:end;if(a<b)extra-=b-a;
        }
        if(extra<=store->defer_limit-store->deferred_bytes){
            if(mprotect(address,size,PROT_NONE))return -1;
            size_t first=0,last;while(first<store->deferred_count&&store->dead[first].offset+store->dead[first].size<offset)first++;
            last=first;
            while(last<store->deferred_count&&store->dead[last].offset<=end){
                if(store->dead[last].offset<offset)offset=store->dead[last].offset;
                size_t hi=store->dead[last].offset+store->dead[last].size;if(hi>end)end=hi;last++;
            }
            memmove(store->dead+first+1,store->dead+last,(store->deferred_count-last)*sizeof *store->dead);
            store->dead[first]=(DmLinuxAliasDead){offset,end-offset};store->deferred_count=store->deferred_count-(last-first)+1;
            store->deferred_bytes+=extra;store->deferred_unmaps++;return 0;
        }
    }
    /* A failed punch must leave the guest's mapping in place. After success,
     * replace only our reserved pages; adjacent live ranges retain bytes. */
    if(fallocate(store->fd,FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE,(off_t)offset,(off_t)size))return -1;
    dm_linux_alias_dead_remove(store,offset,size);store->synchronous_unmaps++;
    return mmap(address,size,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==MAP_FAILED?-1:0;
}
