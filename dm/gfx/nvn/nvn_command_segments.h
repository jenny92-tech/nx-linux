// INPUT: Host command-memory requests and guest ranges explicitly re-granted.
// OUTPUT: Size-bounded segments retained until their recording storage is reused.
// POS: CPU allocator policy; caller serializes every operation under its lock.
#pragma once
#include <stdint.h>
#include <stdlib.h>

#define NVN_HOSTSEG_MIN (4u<<10)
#define NVN_HOSTSEG_DEFAULT (256u<<10)
#define NVN_HOSTSEG_LIMIT (64u<<20)
typedef struct {
    uint8_t *mem;
    size_t size;
    uintptr_t origin_lo,origin_hi;
    uintptr_t owner; /* Command buffer whose current cursor still uses this segment. */
    int busy;
} NvnCommandSegment;
typedef struct {
    NvnCommandSegment *segments;
    size_t count,capacity,bytes;
} NvnCommandSegments;

static inline size_t nvn_command_segment_size(size_t request,size_t minimum) {
    if(request<64)request=minimum?minimum:NVN_HOSTSEG_DEFAULT;
    if(request<minimum)request=minimum;
    if(request>NVN_HOSTSEG_LIMIT)return 0;
    size_t size=NVN_HOSTSEG_MIN;
    while(size<request)size*=2;
    return size;
}
static inline void nvn_command_segments_release(NvnCommandSegments *p,uintptr_t lo,uintptr_t hi) {
    if(lo>=hi)return;
    for(size_t i=0;i<p->count;i++){
        NvnCommandSegment *s=&p->segments[i];
        if(s->busy&&s->origin_lo<s->origin_hi&&s->origin_lo<hi&&lo<s->origin_hi)s->busy=0;
    }
}
/* Only a never-recorded buffer can discard its initial, unexposed segment.
 * Finalizing a buffer that returned a command handle does not kill that handle. */
static inline int nvn_command_segments_unused(NvnCommandSegments *p,uintptr_t cursor,uintptr_t owner) {
    for(size_t i=0;i<p->count;i++){
        NvnCommandSegment *s=&p->segments[i];
        if((uintptr_t)s->mem==cursor&&s->owner==owner&&!s->origin_lo&&!s->origin_hi){s->busy=0;s->owner=0;return 1;}
    }
    return 0;
}
/* Switch only after writing the old segment's JUMP. Guest re-grants may kill
 * a recording while another buffer still has its cursor attached to it. */
static inline void nvn_command_segments_move(NvnCommandSegments *p,uintptr_t owner,uintptr_t cursor) {
    if(!owner)return;
    for(size_t i=0;i<p->count;i++){
        NvnCommandSegment *s=&p->segments[i];uintptr_t at=(uintptr_t)s->mem;
        if(s->owner==owner&&(!cursor||cursor<at||cursor-at>s->size))s->owner=0;
    }
}
/* A fresh handle starting inside an older host segment does not reference
 * that segment's old guest prefix. Its independent lifetime is unknown. */
static inline void nvn_command_segments_pin(NvnCommandSegments *p,uintptr_t cursor,uintptr_t owner) {
    for(size_t i=0;i<p->count&&cursor;i++){
        NvnCommandSegment *s=&p->segments[i];uintptr_t at=(uintptr_t)s->mem;
        if(s->mem&&s->owner==owner&&cursor>=at&&cursor-at<=s->size){s->origin_lo=s->origin_hi=0;s->busy=1;return;}
    }
}
static inline uint8_t *nvn_command_segments_take(NvnCommandSegments *p,size_t size,
    uintptr_t cursor,uintptr_t guest_begin,int recording,uintptr_t owner) {
    if(size<NVN_HOSTSEG_MIN||size>NVN_HOSTSEG_LIMIT||(size&(size-1)))return NULL;
    uintptr_t lo=0,hi=0;int found=0;
    for(size_t i=0;i<p->count&&cursor;i++){
        NvnCommandSegment *s=&p->segments[i];uintptr_t at=(uintptr_t)s->mem;
        if(s->mem&&s->owner==owner&&cursor>=at&&cursor-at<=s->size){lo=s->origin_lo;hi=s->origin_hi;found=1;break;}
    }
    if(!found&&recording&&cursor&&cursor<=UINTPTR_MAX-16){lo=guest_begin&&guest_begin<=cursor?guest_begin:cursor;hi=cursor+16;}
    for(size_t i=0;i<p->count;i++){
        NvnCommandSegment *s=&p->segments[i];
        if(!s->busy&&!s->owner&&s->size==size){s->busy=1;s->origin_lo=lo;s->origin_hi=hi;s->owner=owner;return s->mem;}
    }
    /* Free only already-dead recordings when a different size needs space. */
    for(size_t i=0;i<p->count&&p->bytes>NVN_HOSTSEG_LIMIT-size;i++){
        NvnCommandSegment *s=&p->segments[i];
        if(!s->busy&&!s->owner&&s->mem){p->bytes-=s->size;free(s->mem);*s=(NvnCommandSegment){0};}
    }
    if(p->bytes>NVN_HOSTSEG_LIMIT-size)return NULL;
    size_t at=0;while(at<p->count&&p->segments[at].mem)at++;
    if(at==p->count&&p->count==p->capacity){
        size_t cap=p->capacity?p->capacity*2:64;
        if(cap>NVN_HOSTSEG_LIMIT/NVN_HOSTSEG_MIN)return NULL;
        void *mem=realloc(p->segments,cap*sizeof(*p->segments));if(!mem)return NULL;
        p->segments=mem;p->capacity=cap;
    }
    uint8_t *mem=malloc(size);if(!mem)return NULL;
    p->segments[at]=(NvnCommandSegment){mem,size,lo,hi,owner,1};
    if(at==p->count)p->count++;
    p->bytes+=size;return mem;
}
