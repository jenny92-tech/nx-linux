// INPUT: Validated resource ranges, optional dirty spans and queued GPU writes.
// OUTPUT: Exact CPU-owned transfer spans, independent of the graphics backend.
// POS: Shared range planning for mapped copies, GLES uploads and read snapshots.
#ifndef NVN_TRANSFER_H
#define NVN_TRANSFER_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {uint64_t addr,size;} NvnTransferRange;
typedef int (*NvnTransferDirty)(const void *,size_t,uint64_t,size_t *,size_t *,size_t *);
typedef void (*NvnTransferEmit)(void *,uint64_t,uint64_t);

/* The caller validates both allocations and guarantees that all older GPU
 * writes have reached the CPU mirror. In-flight writers must all be supplied;
 * equal stale CPU mirrors never prove a GPU copy redundant. No bytes change. */
static inline int nvn_transfer_cpu_copy_unchanged(uint64_t src,uint64_t dst,uint64_t size,
        const NvnTransferRange *writes,size_t count){
    if(!size||size>SIZE_MAX||size>UINT64_MAX-src||size>UINT64_MAX-dst||(count&&!writes))return 0;
    for(size_t i=0;i<count;i++){NvnTransferRange w=writes[i];
        if(w.size>UINT64_MAX-w.addr)return 0;
        if(w.size&&((src<w.addr+w.size&&w.addr<src+size)||(dst<w.addr+w.size&&w.addr<dst+size)))return 0;
    }
    return !memcmp((const void *)(uintptr_t)src,(const void *)(uintptr_t)dst,(size_t)size);
}

/* No allocation or graphics calls. The caller retains range identity/lifetime.
 * A dirty iterator must return ordered, nonoverlapping, nonempty subranges.
 * UINT64_MAX rejects invalid arithmetic/iterator output; emit is synchronous. */
static inline uint64_t nvn_transfer_visit(uint64_t addr,uint64_t size,
        const NvnTransferRange *writes,size_t count,uint64_t stamp,
        NvnTransferDirty dirty,NvnTransferEmit emit,void *context){
    if(size>SIZE_MAX||size>UINT64_MAX-addr||!emit||(count&&!writes))return UINT64_MAX;
    for(size_t i=0;i<count;i++)if(writes[i].size>UINT64_MAX-writes[i].addr)return UINT64_MAX;
    size_t cursor=0,offset=0,length=(size_t)size,previous_end=0;
    uint64_t copied=0;
    for(;;){
        if(dirty){size_t before=cursor;
            if(!dirty((const void *)(uintptr_t)addr,(size_t)size,stamp,&cursor,&offset,&length))break;
            if(cursor<=before||!length||offset<previous_end||offset>size||length>size-offset)return UINT64_MAX;
            previous_end=offset+length;
        }
        uint64_t at=addr+offset,end=at+length;
        while(at<end){uint64_t next=end,covered=at;
            for(size_t i=0;i<count;i++){NvnTransferRange w=writes[i];
                if(w.addr<=at&&at<w.addr+w.size){if(w.addr+w.size>covered)covered=w.addr+w.size;}
                else if(w.size&&w.addr>at&&w.addr<next)next=w.addr;
            }
            if(covered>at){at=covered<end?covered:end;continue;}
            emit(context,at,next-at);copied+=next-at;at=next;
        }
        if(!dirty)break;
    }
    return copied;
}
/* Visit the clipped union of current GPU writes exactly once, in address
 * order. GPU authority is independent of CPU dirty epochs, including repeated
 * writes to an unchanged address in consecutive submissions. */
static inline uint64_t nvn_transfer_visit_written(uint64_t addr,uint64_t size,
        const NvnTransferRange *writes,size_t count,NvnTransferEmit emit,void *context){
    if(size>SIZE_MAX||size>UINT64_MAX-addr||!emit||(count&&!writes))return UINT64_MAX;
    for(size_t i=0;i<count;i++)if(writes[i].size>UINT64_MAX-writes[i].addr)return UINT64_MAX;
    uint64_t at=addr,end=addr+size,copied=0;
    while(at<end){uint64_t first=end,last=at;
        for(size_t i=0;i<count;i++){NvnTransferRange w=writes[i];
            if(!w.size||w.addr>=end||w.addr+w.size<=at)continue;
            uint64_t lo=w.addr<at?at:w.addr;
            if(lo<first)first=lo;
        }
        if(first==end)break;
        last=first;
        for(;;){uint64_t before=last;
            for(size_t i=0;i<count;i++){NvnTransferRange w=writes[i];
                if(w.size&&w.addr<=last&&w.addr+w.size>last)last=w.addr+w.size;
            }
            if(last==before||last>=end)break;
        }
        if(last>end)last=end;
        emit(context,first,last-first);copied+=last-first;at=last;
    }
    return copied;
}
#endif
