// INPUT: Zero/pattern/random baselines, corrupt streams and a mixed-owner block fixture.
// OUTPUT: Exact round trips, raw fallback and unchanged GPU-owned blocks.
// POS: Standalone lossless CPU snapshot regression; no GPU or game state.
#include "../../gfx/nvn/nvn_vk_snapshot.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void roundtrip(size_t size,int random_bytes) {
    uint8_t *raw=malloc(size),*expected=malloc(size);assert(raw&&expected);
    uint32_t state=0x59a3f17b;
    for(size_t i=0;i<size;i++){
        state^=state<<13;state^=state>>17;state^=state<<5;
        raw[i]=random_bytes?(uint8_t)state:(uint8_t)((i/1024)%7);
    }
    memcpy(expected,raw,size);uint8_t *original=raw;
    size_t stored=nvn_vk_snapshot_pack(&raw,size);assert(stored<=size);
    if(size<1024||random_bytes)assert(stored==size&&raw==original);
    else assert(stored<=size-size/8);
    uint8_t *scratch=NULL;const uint8_t *view=nvn_vk_snapshot_open(raw,stored,size,&scratch);
    assert(view&&!memcmp(view,expected,size));assert((scratch!=NULL)==(stored<size));
    free(scratch);
    if(stored<size){
        raw[stored-1]^=1;
        assert(!nvn_vk_snapshot_open(raw,stored,size,&scratch)&&!scratch);
        raw[stored-1]^=1;
        assert(!nvn_vk_snapshot_open(raw,stored-1,size,&scratch)&&!scratch);
        assert(!nvn_vk_snapshot_open(raw,stored,size+1,&scratch)&&!scratch);
    }
    free(raw);free(expected);
}

static void mixed_blocks(void) {
    enum { SIZE=65536,BLOCK=16 };
    uint8_t *baseline=calloc(1,SIZE),next[SIZE]={0},gpu[SIZE]={0};assert(baseline);
    size_t stored=nvn_vk_snapshot_pack(&baseline,SIZE);assert(stored<SIZE);
    /* A GPU copy changed one block; the CPU independently streamed another.
     * Compare against the exact old CPU baseline, never the GPU image. */
    memset(gpu+3*BLOCK,0xa5,BLOCK);memset(next+7*BLOCK,0x79,BLOCK);
    uint8_t *scratch=NULL;const uint8_t *old=nvn_vk_snapshot_open(baseline,stored,SIZE,&scratch);assert(old);
    unsigned changed=0;
    for(size_t i=0;i<SIZE;i+=BLOCK)if(memcmp(old+i,next+i,BLOCK)){
        memcpy(gpu+i,next+i,BLOCK);changed++;
    }
    assert(changed==1);
    for(unsigned i=0;i<BLOCK;i++){assert(gpu[3*BLOCK+i]==0xa5);assert(gpu[7*BLOCK+i]==0x79);}
    free(scratch);free(baseline);
    baseline=malloc(SIZE);assert(baseline);memcpy(baseline,next,SIZE);
    stored=nvn_vk_snapshot_pack(&baseline,SIZE);
    old=nvn_vk_snapshot_open(baseline,stored,SIZE,&scratch);assert(old&&!memcmp(old,next,SIZE));
    free(scratch);free(baseline);
}

int main(void) {
    const size_t sizes[]={1,1023,1024,1025,16384,65537,1048576};
    for(unsigned i=0;i<sizeof sizes/sizeof *sizes;i++){roundtrip(sizes[i],0);roundtrip(sizes[i],1);}
    uint8_t *scratch=(uint8_t *)1;
    assert(!nvn_vk_snapshot_open(NULL,0,0,&scratch)&&!scratch);
    mixed_blocks();puts("snapshot codec: exact bytes, fallback, corruption and mixed-owner updates passed");return 0;
}
