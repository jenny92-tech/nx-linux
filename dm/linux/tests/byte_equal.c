// INPUT: Synthetic control/code bytes, every mutation and page-end ranges.
// OUTPUT: Agreement with memcmp equality, including alignment and guard-page tails.
// POS: Host ASan/UBSan regression; no game data or runtime memory reads.
#define _GNU_SOURCE
#include "../../gfx/nvn/nvn_vk_bytes.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
int main(void){
    uint8_t a[8256],b[8256];for(unsigned i=0;i<sizeof a;i++)a[i]=(uint8_t)(i*73u+(i>>5));
    for(unsigned x=0;x<32;x++)for(unsigned y=0;y<32;y++){
        memcpy(b+y,a+x,8192);assert(nvn_bytes_equal(a+x,b+y,8192));
        for(unsigned n=0;n<130;n++)assert(nvn_bytes_equal(a+x,b+y,n)==!memcmp(a+x,b+y,n));
    }
    memcpy(b,a,8192);
    for(unsigned i=0;i<8192;i++){b[i]^=0x81;assert(!nvn_bytes_equal(a,b,8192));b[i]^=0x81;}
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    uint8_t *p=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    uint8_t *q=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    assert(p!=MAP_FAILED&&q!=MAP_FAILED);
    assert(!mprotect(p+page,page,PROT_NONE)&&!mprotect(q+page,page,PROT_NONE));
    memset(p,0x59,page);memset(q,0x59,page);
    for(unsigned n=0;n<512;n++)assert(nvn_bytes_equal(p+page-n,q+page-n,n));
    munmap(p,page*2);munmap(q,page*2);
    puts("byte equality: alignments, 8192 independent byte mutations and guard-page tails passed");
}
