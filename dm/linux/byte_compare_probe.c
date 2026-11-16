// INPUT: Synthetic cached/uncached buffers and the existing system-uncached DMA heap.
// OUTPUT: libc/vector/libc boolean-equality CPU timings with exact result validation.
// POS: Bounded device microbenchmark; no game files, hooks, controls or clock writes.
#include "../gfx/nvn/nvn_vk_bytes.h"
#include <fcntl.h>
#include <inttypes.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
static uint64_t cpu(void){struct timespec t;if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t))exit(1);return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;}
static __attribute__((noinline)) int libc_equal(const void *a,const void *b,size_t n){__asm__ volatile("":::"memory");return !memcmp(a,b,n);}
static __attribute__((noinline)) int vector_equal(const void *a,const void *b,size_t n){__asm__ volatile("":::"memory");return nvn_bytes_equal(a,b,n);}
int main(void){
    int heap=open("/dev/dma_heap/system-uncached",O_RDWR|O_CLOEXEC);if(heap<0){perror("heap");return 1;}
    struct dma_heap_allocation_data allocation={.len=16384,.fd_flags=O_RDWR|O_CLOEXEC};
    if(ioctl(heap,DMA_HEAP_IOCTL_ALLOC,&allocation)){perror("DMA allocation");return 1;}close(heap);
    int fd=(int)allocation.fd;uint8_t *dma=mmap(NULL,16384,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0),*a=malloc(16384),*b=malloc(16384);
    if(dma==MAP_FAILED||!a||!b)return 1;
    struct dma_buf_sync sync={.flags=DMA_BUF_SYNC_START|DMA_BUF_SYNC_RW};if(ioctl(fd,DMA_BUF_IOCTL_SYNC,&sync))return 1;
    for(unsigned i=0;i<16384;i++)a[i]=(uint8_t)(i*71u+(i>>9));memcpy(dma,a,16384);memcpy(b,a,16384);
    size_t sizes[]={2176,8192};
    for(unsigned memory=0;memory<2;memory++)for(unsigned size=0;size<2;size++)for(unsigned phase=0;phase<3;phase++){
        const void *source=memory?(const void *)dma:(const void *)b;
        int (*compare)(const void *,const void *,size_t)=phase==1?vector_equal:libc_equal;
        uint64_t start=cpu();unsigned result=0;
        for(unsigned i=0;i<20000;i++)result+=(unsigned)compare(a,source,sizes[size]);
        uint64_t elapsed=cpu()-start;if(result!=20000)return 1;
        printf("{\"uncached\":%s,\"bytes\":%zu,\"phase\":%u,\"vector\":%s,\"calls\":20000,\"thread_cpu_ns\":%"PRIu64",\"ns_per_call\":%.3f,\"exact_results\":true}\n",memory?"true":"false",sizes[size],phase,phase==1?"true":"false",elapsed,(double)elapsed/20000);fflush(stdout);
    }
    sync.flags=DMA_BUF_SYNC_END|DMA_BUF_SYNC_RW;if(ioctl(fd,DMA_BUF_IOCTL_SYNC,&sync))return 1;
    munmap(dma,16384);close(fd);free(a);free(b);puts("{\"complete\":true,\"gameplay_fps_measurement\":false}");
}
