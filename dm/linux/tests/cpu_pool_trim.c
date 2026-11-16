// INPUT: Synthetic TLSF free lists and real Linux private anonymous pages.
// OUTPUT: Valid/invalid plan checks and unchanged metadata/live bytes after reclaim.
// POS: CPU-pool regression; no game files or graphics dependencies.
#include "../cpu_pool_trim.h"
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>
static void w64(uintptr_t at,uint64_t n){memcpy((void *)at,&n,8);}
static void w32(uintptr_t at,uint32_t n){memcpy((void *)at,&n,4);}
static void reset(uintptr_t base,size_t bytes){
    memset((void *)base,0x7b,bytes);memset((void *)base,0,0x1b90);
    for(unsigned f=0;f<27;f++)for(unsigned l=0;l<32;l++)w64(base+0x90+f*256+l*8,base);
}
static uintptr_t add(uintptr_t base,uintptr_t head,size_t n){
    unsigned h=63-(unsigned)__builtin_clzll(n),f=n<256?0:h-7,l=n<256?(unsigned)n/8:(unsigned)((n>>(h-5))^32);
    uintptr_t slot=base+0x90+f*256+l*8,old=dm_cpu_trim_u64(slot);
    w32(base+0x20,dm_cpu_trim_u32(base+0x20)|(1u<<f));w32(base+0x24+f*4,dm_cpu_trim_u32(base+0x24+f*4)|(1u<<l));
    w64(head+8,n|1);w64(head+16,old);w64(head+24,base);if(old!=base)w64(old+24,head);w64(slot,head);
    w64(head+n+16,34);return slot;
}
int main(void){
    const size_t bytes=2*1024*1024;void *mapping=mmap(NULL,bytes+16384,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(mapping!=MAP_FAILED);
    uintptr_t base=((uintptr_t)mapping+16383)&~(uintptr_t)16383;DmCpuTrimRange ranges[32];DmCpuTrimPlan p;
    for(size_t page=4096;page<=16384;page*=4){
        reset(base,bytes);uintptr_t head=base+0x3008;size_t n=65536+88;add(base,head,n);
        assert(dm_cpu_trim_plan(base,bytes,page,ranges,32,&p)&&p.blocks==1&&p.ranges==1&&p.free_bytes==n);
        assert(ranges[0].address==((head+32+page-1)&~(page-1))&&ranges[0].address+ranges[0].bytes==((head+n+8)&~(page-1)));
        unsigned char *before=malloc(bytes);assert(before);memcpy(before,(void *)base,bytes);
        DmCpuTrimRange r=ranges[0];assert(!madvise((void *)r.address,r.bytes,MADV_DONTNEED));
        for(size_t i=0;i<bytes;i++)assert(((unsigned char *)base)[i]==(base+i>=r.address&&base+i<r.address+r.bytes?0:before[i]));
        assert(dm_cpu_trim_plan(base,bytes,page,ranges,32,&p)&&p.blocks==1);free(before);
        memset((void *)r.address,0xa6,r.bytes);for(size_t i=0;i<r.bytes;i++)assert(((unsigned char *)r.address)[i]==0xa6);
    }
    reset(base,bytes);uintptr_t tiny=base+0x3000,large=base+0x80000;add(base,tiny,24);add(base,large,128*1024+24);
    assert(dm_cpu_trim_plan(base,bytes,4096,ranges,32,&p)&&p.blocks==2&&p.ranges==1);
    // Out-of-range links, cycles, corrupt physical markers and wrong bucket bits
    // must all decline before any page can be passed to madvise.
    reset(base,bytes);uintptr_t head=base+0x5000;uintptr_t slot=add(base,head,65536);w64(head+16,base+bytes+8);assert(!dm_cpu_trim_plan(base,bytes,4096,ranges,32,&p));
    reset(base,bytes);slot=add(base,head,65536);w64(head+16,head);w64(head+24,head);assert(!dm_cpu_trim_plan(base,bytes,4096,ranges,32,&p));
    reset(base,bytes);slot=add(base,head,65536);w64(head+65536+16,0);assert(!dm_cpu_trim_plan(base,bytes,4096,ranges,32,&p));
    reset(base,bytes);slot=add(base,head,65536);w32(base+0x20,0);assert(!dm_cpu_trim_plan(base,bytes,4096,ranges,32,&p));
    reset(base,bytes);slot=add(base,head,65536);w64(slot+8,head);assert(!dm_cpu_trim_plan(base,bytes,4096,ranges,32,&p));
    reset(base,bytes);add(base,head,256*1024);add(base,head+32768,65536);assert(!dm_cpu_trim_plan(base,bytes,4096,ranges,32,&p));
    reset(base,bytes);add(base,head,65536);assert(!dm_cpu_trim_plan(base,bytes,4096,ranges,0,&p));
    assert(!dm_cpu_trim_plan(base,bytes,3000,ranges,32,&p));
    assert(!munmap(mapping,bytes+16384));puts("CPU trim plan: valid layouts, metadata/live bytes, reuse, compacted ranges and corrupt inputs passed");
}
