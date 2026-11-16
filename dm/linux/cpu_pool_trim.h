// INPUT: A TLSF control area while its original allocator mutex is held.
// OUTPUT: Validated whole-page free interiors; metadata and live blocks excluded.
// POS: Linux 94012 CPU-pool reclamation; never changes pool capacity.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

typedef struct {uintptr_t address;size_t bytes;} DmCpuTrimRange;
typedef struct {size_t blocks,free_bytes,ranges,interior_bytes;} DmCpuTrimPlan;
static inline uint64_t dm_cpu_trim_u64(uintptr_t p){uint64_t n;memcpy(&n,(void *)p,8);return n;}
static inline uint32_t dm_cpu_trim_u32(uintptr_t p){uint32_t n;memcpy(&n,(void *)p,4);return n;}
static inline int dm_cpu_trim_order(const void *a,const void *b){
    uintptr_t x=((const DmCpuTrimRange *)a)->address,y=((const DmCpuTrimRange *)b)->address;
    return x>y?1:x<y?-1:0;
}
static inline int dm_cpu_trim_plan(uintptr_t base,size_t bytes,size_t page,
        DmCpuTrimRange *ranges,size_t capacity,DmCpuTrimPlan *out){
    DmCpuTrimPlan plan={0};
    if(!base||(base&7)||bytes<0x2000||base>UINTPTR_MAX-bytes||!page||(page&(page-1))||!ranges||!out)return 0;
    uintptr_t end=base+bytes;uint32_t fl=dm_cpu_trim_u32(base+0x20);
    if(fl>>27)return 0;
    for(unsigned f=0;f<27;f++){
        uint32_t sl=dm_cpu_trim_u32(base+0x24+f*4);
        if(!!(fl&(1u<<f))!=!!sl)return 0;
        for(unsigned l=0;l<32;l++){
            uintptr_t head=dm_cpu_trim_u64(base+0x90+f*256+l*8),previous=base;
            if(!(sl&(1u<<l))){if(head!=base)return 0;continue;}
            if(head==base)return 0;
            while(head!=base){
                if(plan.blocks==capacity||(head&7)||head<base+0x1b80||head>end-32)return 0;
                uint64_t flags=dm_cpu_trim_u64(head+8);size_t n=(size_t)(flags&~UINT64_C(3));
                if((flags&3)!=1||n<24||(n&7)||n>end-head-24||dm_cpu_trim_u64(head+24)!=previous)return 0;
                unsigned high=63u-(unsigned)__builtin_clzll(n);
                unsigned bf=n<256?0:high-7,bl=n<256?(unsigned)(n/8):(unsigned)((n>>(high-5))^32);
                if(bf!=f||bl!=l||!(dm_cpu_trim_u64(head+n+16)&2))return 0;
                ranges[plan.blocks++]=(DmCpuTrimRange){head,n+8};
                if(plan.free_bytes>SIZE_MAX-n)return 0;
                plan.free_bytes+=n;previous=head;head=dm_cpu_trim_u64(head+16);
            }
        }
    }
    qsort(ranges,plan.blocks,sizeof *ranges,dm_cpu_trim_order);
    uintptr_t previous_end=0;
    for(size_t i=0;i<plan.blocks;i++){
        uintptr_t head=ranges[i].address,end_block=head+ranges[i].bytes;
        if(i&&head<previous_end)return 0;
        previous_end=end_block;
        // Block links occupy its first 32 bytes. The next block header begins
        // at end_block; neither it nor a partial boundary page may be discarded.
        if(head+32>UINTPTR_MAX-(page-1))return 0;
        uintptr_t lo=(head+32+page-1)&~(uintptr_t)(page-1),hi=end_block&~(uintptr_t)(page-1);
        if(hi>lo){ranges[plan.ranges++]=(DmCpuTrimRange){lo,hi-lo};plan.interior_bytes+=hi-lo;}
    }
    *out=plan;return 1;
}
