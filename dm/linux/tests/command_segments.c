// INPUT: Small/large command requests and explicit guest recording lifetimes.
// OUTPUT: Allocation-budget, long-lived-recording and origin-reuse checks.
// POS: Host-only allocator regression; no NVN driver, GPU or device required.
#include "../../gfx/nvn/nvn_command_segments.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"command segments failed line %d: %s\n",__LINE__,#x);exit(1);}} while(0)
static void clear(NvnCommandSegments *p){for(size_t i=0;i<p->count;i++)free(p->segments[i].mem);free(p->segments);*p=(NvnCommandSegments){0};}
/* Completed recordings in these policy tests have no attached writer. */
static uint8_t *take(NvnCommandSegments *p,size_t size,uintptr_t cursor,uintptr_t guest,int recording){
    return nvn_command_segments_take(p,size,cursor,guest,recording,0);
}

static void sizes_and_live(void){
    CHECK(nvn_command_segment_size(512,0)==4096);
    CHECK(nvn_command_segment_size(0,0)==256*1024);
    CHECK(nvn_command_segment_size(4097,0)==8192);
    CHECK(nvn_command_segment_size(0,300*1024)==512*1024);
    CHECK(nvn_command_segment_size(512,1024*1024+24)==2*1024*1024);
    CHECK(!nvn_command_segment_size((size_t)NVN_HOSTSEG_LIMIT+1,0));
    NvnCommandSegments p={0};uint8_t *live[256];
    for(unsigned i=0;i<256;i++){
        live[i]=take(&p,4096,0,0,0);CHECK(live[i]);
        memset(live[i],(uint8_t)i,4096);
    }
    CHECK(p.count==256&&p.bytes==1024*1024);
    /* More allocations cannot reclaim an older cached recording by frame age. */
    for(unsigned i=0;i<1024;i++)CHECK(take(&p,4096,0,0,1));
    for(unsigned i=0;i<256;i++)for(unsigned j=0;j<4096;j++)CHECK(live[i][j]==(uint8_t)i);
    CHECK(nvn_command_segments_unused(&p,(uintptr_t)live[0],0));
    CHECK(take(&p,4096,0,0,0)==live[0]);
    CHECK(!take(&p,512,0,0,0));
    CHECK(!take(&p,6144,0,0,0));
    clear(&p);
}

static void origins(void){
    uint8_t guest[512];uintptr_t base=(uintptr_t)guest;
    NvnCommandSegments p={0};
    uint8_t *first=take(&p,4096,base+128,base+32,1);CHECK(first);
    memset(first,0x35,4096);
    uint8_t *overflow=take(&p,8192,(uintptr_t)first+4080,base+32,1);CHECK(overflow);
    CHECK(p.segments[0].origin_lo==base+32&&p.segments[0].origin_hi==base+144);
    CHECK(p.segments[1].origin_lo==base+32&&p.segments[1].origin_hi==base+144);
    nvn_command_segments_release(&p,base,base+32);
    nvn_command_segments_release(&p,base+144,base+200);
    CHECK(p.segments[0].busy&&p.segments[1].busy);
    for(unsigned i=0;i<64;i++)CHECK(take(&p,4096,0,0,0)!=first);
    for(unsigned i=0;i<4096;i++)CHECK(first[i]==0x35);
    nvn_command_segments_release(&p,base+100,base+101);
    CHECK(!p.segments[0].busy&&!p.segments[1].busy);
    CHECK(take(&p,4096,0,0,1)==first);
    CHECK(take(&p,8192,0,0,1)==overflow);
    /* An exposed recording with no guest origin remains pinned, including a
     * zero/invalid re-grant. Its handle has no proven reclamation event. */
    nvn_command_segments_release(&p,0,0);
    nvn_command_segments_release(&p,base,base+sizeof guest);
    CHECK(p.segments[0].busy&&p.segments[1].busy);
    clear(&p);
}

static void budget(void){
    NvnCommandSegments p={0};uint8_t guest[64];
    uint8_t *large=take(&p,NVN_HOSTSEG_LIMIT,(uintptr_t)guest+32,(uintptr_t)guest,1);CHECK(large);
    CHECK(p.bytes==NVN_HOSTSEG_LIMIT);
    CHECK(!take(&p,4096,0,0,0));CHECK(p.bytes==NVN_HOSTSEG_LIMIT);
    nvn_command_segments_release(&p,(uintptr_t)guest,(uintptr_t)guest+64);
    CHECK(take(&p,4096,0,0,0));CHECK(p.bytes==4096&&p.count==1);
    clear(&p);
}

static void independent_recordings(void){
    uint8_t guest[1024];uintptr_t base=(uintptr_t)guest;NvnCommandSegments p={0};
    /* R begins in A, moves to non-adjacent B, then overflows. The origin
     * passed by AddCommandMemory is B's start, never R's old start in A. */
    uint8_t *h=take(&p,4096,base+768,base+640,1);CHECK(h);
    CHECK(p.segments[0].origin_lo==base+640);
    nvn_command_segments_release(&p,base+256,base+512);
    CHECK(p.segments[0].busy);
    nvn_command_segments_release(&p,base,base+128);CHECK(p.segments[0].busy);
    /* R2 starts in H itself: its handle is independent of B. */
    nvn_command_segments_pin(&p,(uintptr_t)h+64,0);
    CHECK(!p.segments[0].origin_lo&&!p.segments[0].origin_hi);
    uint8_t *tail=take(&p,4096,(uintptr_t)h+4000,(uintptr_t)h+64,1);CHECK(tail);
    nvn_command_segments_release(&p,base+640,base+784);
    CHECK(p.segments[0].busy&&p.segments[1].busy);
    /* A BEGIN that itself needed a host segment is allocated with no old
     * cursor/origin, under the same allocator lock, before it is exposed. */
    uint8_t *begin=take(&p,4096,0,0,0);CHECK(begin);
    nvn_command_segments_release(&p,base,base+sizeof guest);
    CHECK(p.segments[2].busy&&!p.segments[2].origin_lo&&!p.segments[2].origin_hi);
    clear(&p);
}

static void attached_cursors(void){
    NvnCommandSegments p={0};uint8_t guest[256];uintptr_t base=(uintptr_t)guest;
    const uintptr_t c1=1,c2=2;
    uint8_t *h=nvn_command_segments_take(&p,4096,base+64,base,1,c1);CHECK(h);
    memset(h,0x6d,4096);
    nvn_command_segments_release(&p,base,base+80);
    CHECK(!p.segments[0].busy&&p.segments[0].owner==c1);
    /* Re-granting A kills the old handle, but C1 still writes through H. */
    uint8_t *other=nvn_command_segments_take(&p,4096,0,0,0,c2);CHECK(other&&other!=h);
    for(unsigned i=0;i<4096;i++)CHECK(h[i]==0x6d);
    nvn_command_segments_pin(&p,(uintptr_t)h+64,c2);
    CHECK(!p.segments[0].busy&&p.segments[0].origin_lo==base);
    nvn_command_segments_pin(&p,(uintptr_t)h+64,c1);
    CHECK(p.segments[0].busy&&!p.segments[0].origin_lo);
    /* A new record in H survives C1 switching segments and Finalize. */
    uint8_t *next=nvn_command_segments_take(&p,4096,(uintptr_t)h+4000,(uintptr_t)h+64,1,c1);CHECK(next);
    CHECK(p.segments[0].owner==c1&&p.segments[2].owner==c1);
    nvn_command_segments_move(&p,c1,(uintptr_t)next);
    CHECK(!p.segments[0].owner&&p.segments[2].owner==c1);
    nvn_command_segments_move(&p,c1,0);
    CHECK(!p.segments[2].owner&&p.segments[0].busy&&p.segments[2].busy);
    CHECK(nvn_command_segments_unused(&p,(uintptr_t)other,c2));
    CHECK(!p.segments[1].owner&&!p.segments[1].busy);
    CHECK(nvn_command_segments_take(&p,4096,0,0,0,c2)==other);
    clear(&p);
    /* Even under the byte cap, a dead record with an attached writer may
     * not be freed to satisfy a differently-sized request. Detach then frees. */
    h=nvn_command_segments_take(&p,NVN_HOSTSEG_LIMIT,base+64,base,1,c1);CHECK(h);
    nvn_command_segments_release(&p,base,base+80);
    CHECK(!nvn_command_segments_take(&p,4096,0,0,0,c2));
    h[0]=0x23;h[NVN_HOSTSEG_LIMIT-1]=0x45;
    CHECK(h[0]==0x23&&h[NVN_HOSTSEG_LIMIT-1]==0x45);
    nvn_command_segments_move(&p,c1,0);
    CHECK(nvn_command_segments_take(&p,4096,0,0,0,c2));CHECK(p.bytes==4096);
    clear(&p);
}

static NvnCommandSegments threaded;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static int thread_failed;
static void *worker(void *unused){
    (void)unused;
    for(unsigned i=0;i<1000;i++){
        pthread_mutex_lock(&lock);
        uint8_t *p=take(&threaded,4096,0,0,0);
        if(!p)thread_failed=1;
        else {p[0]=0x39;nvn_command_segments_unused(&threaded,(uintptr_t)p,0);}
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}
int main(void){
    sizes_and_live();origins();budget();independent_recordings();attached_cursors();
    pthread_t threads[4];for(unsigned i=0;i<4;i++)CHECK(!pthread_create(&threads[i],NULL,worker,NULL));
    for(unsigned i=0;i<4;i++)CHECK(!pthread_join(threads[i],NULL));
    CHECK(!thread_failed&&threaded.bytes==4096&&threaded.count==1);clear(&threaded);
    printf("COMMAND_SEGMENTS_OK checks=%u small_grants=256 retained_bytes=1048576 budget_bytes=%u frame_age_reuse=0\n",checks,NVN_HOSTSEG_LIMIT);
    return 0;
}
