// INPUT: Synthetic NVN R8/RGBA8 images and the existing uncached DMA heap.
// OUTPUT: Byte-exact uncached/cached-copy/uncached timings, including copy cost.
// POS: Bounded device microbenchmark; no game assets, game input or clock writes.
#include "../gfx/tex/nvn_deswizzle.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static uint64_t ns(clockid_t id) {
    struct timespec t;if(clock_gettime(id,&t)){perror("clock");exit(1);}
    return (uint64_t)t.tv_sec*1000000000ull+(uint64_t)t.tv_nsec;
}
static int image(int format,unsigned bpp) {
    nvn_texdesc d={.target=1,.format=format,.width=1024,.height=1024,.depth=1,.levels=1};
    nvn_texlayout layout;nvn_tex_layout(&d,&layout);
    if(!layout.size||layout.size>8u*1024u*1024u)return 1;
    size_t bytes=(size_t)d.width*d.height*bpp,allocation=(layout.size+4095u)&~4095ull;
    uint8_t *expected=malloc(bytes),*linear=malloc(bytes),*tiled=calloc(1,allocation),*scratch=malloc(allocation);
    if(!expected||!linear||!tiled||!scratch)return 1;
    for(size_t i=0;i<bytes;i++)expected[i]=(uint8_t)((i*71u)^(i>>8)^0x95u);
    if(nvn_swizzle(&d,0,0,expected,(size_t)d.width*bpp,tiled))return 1;
    int heap=open("/dev/dma_heap/system-uncached",O_RDWR|O_CLOEXEC);
    if(heap<0){perror("uncached heap");return 1;}
    struct dma_heap_allocation_data a={.len=allocation,.fd_flags=O_RDWR|O_CLOEXEC};
    int result=ioctl(heap,DMA_HEAP_IOCTL_ALLOC,&a);close(heap);
    if(result){perror("DMA allocation");return 1;}
    int fd=(int)a.fd;void *dma=mmap(NULL,allocation,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(dma==MAP_FAILED){perror("DMA mmap");close(fd);return 1;}
    struct dma_buf_sync sync={.flags=DMA_BUF_SYNC_START|DMA_BUF_SYNC_RW};
    if(ioctl(fd,DMA_BUF_IOCTL_SYNC,&sync)){perror("CPU ownership");return 1;}
    memcpy(dma,tiled,layout.size);
    for(unsigned phase=0;phase<3;phase++) {
        uint64_t wall=ns(CLOCK_MONOTONIC),cpu=ns(CLOCK_THREAD_CPUTIME_ID);
        const void *input=dma;
        if(phase==1){memcpy(scratch,dma,layout.size);input=scratch;}
        result=nvn_deswizzle(&d,0,0,input,linear,(size_t)d.width*bpp);
        cpu=ns(CLOCK_THREAD_CPUTIME_ID)-cpu;wall=ns(CLOCK_MONOTONIC)-wall;
        if(result||memcmp(expected,linear,bytes)){fprintf(stderr,"byte comparison failed\n");return 1;}
        printf("{\"format\":%d,\"phase\":%u,\"cached_copy\":%s,\"storage_bytes\":%"PRIu64",\"wall_ns\":%"PRIu64",\"thread_cpu_ns\":%"PRIu64",\"byte_exact\":true}\n",
            format,phase,phase==1?"true":"false",layout.size,wall,cpu);fflush(stdout);
    }
    sync.flags=DMA_BUF_SYNC_END|DMA_BUF_SYNC_RW;
    if(ioctl(fd,DMA_BUF_IOCTL_SYNC,&sync)){perror("CPU ownership release");return 1;}
    munmap(dma,allocation);close(fd);free(scratch);free(tiled);free(linear);free(expected);return 0;
}
int main(void) {
    if(image(0x1,1)||image(0x25,4))return 1;
    puts("{\"complete\":true,\"heap\":\"system-uncached\",\"game_data_used\":false,\"gameplay_fps_measurement\":false}");return 0;
}
