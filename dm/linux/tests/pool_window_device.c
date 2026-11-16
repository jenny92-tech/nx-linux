// INPUT: Production Vulkan backend, a synthetic logical pool and native SDL surface.
// OUTPUT: Actual GPU byte/order, alias, promotion and fence-owned reclamation checks.
// POS: Independent device fixture; no original game, saves or production test hooks.
#include "../../host/hle.h"
#undef die
#include "../../gfx/nvn/nvn_vk.c"
#include "../memory_guard.h"
static SDL_Window *probe_window;
void crash_thread_init(void);
void *dm_android_native_window(void){return probe_window;}
void dm_android_front_presented(uint64_t frame){(void)frame;}
double front_display_refresh(char *name,size_t capacity){snprintf(name,capacity,"Pool window fixture");return 60;}
void dm_host_shutdown(const char *why){fprintf(stderr,"Unexpected shutdown: %s\n",why);_exit(2);}
void dm_shot_written(const char *path,int ok){(void)path;(void)ok;}
static unsigned checks;
#define REQUIRE(x) do{checks++;if(!(x)){fprintf(stderr,"POOL_WINDOW_FAIL line=%d expression=%s\n",__LINE__,#x);abort();}}while(0)

static VBuffer capture_view(VPool *view,uint64_t address,uint64_t bytes){
    REQUIRE(address>=(uint64_t)(uintptr_t)view->guest&&bytes<=view->size-(address-(uint64_t)(uintptr_t)view->guest));
    VBuffer out=buffer_create_kind(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT,1,DM_VK_MEMORY_READBACK);
    all_barrier();VkBufferCopy copy={address-(uint64_t)(uintptr_t)view->guest,0,bytes};
    vkCmdCopyBuffer(S.command,view->v.b,out.b,1,&copy);return out;
}
static VPool *read_view(uint64_t address,uint64_t bytes){uint64_t off=0;return pool_read(address,bytes,&off);}
static VBuffer capture_bytes(uint64_t address,uint64_t bytes){return capture_view(read_view(address,bytes),address,bytes);}
static void expect(VBuffer *actual,const void *expected,size_t bytes){REQUIRE(!memcmp(actual->map,expected,bytes));buffer_free(actual);}
static void queued_cpu_update(uint8_t *command_memory,uint64_t address,const uint8_t payload[16]){
    struct {op_hdr head;uint64_t address,bytes;uint8_t payload[16];op_hdr end;} command={
        .head={OP_UPDATE_UB,40},.address=address,.bytes=16,.end={OP_END,8}};
    memcpy(command.payload,payload,16);memcpy(command_memory,&command,sizeof command);mtl_exec(command_memory);
}
typedef struct {VkEvent event;uint64_t previous_waits;int timed_out;} UploadGate;
static void *release_upload_gate(void *opaque){
    UploadGate *gate=opaque;uint64_t deadline=dm_android_nvn_clock_ns()+5000000000ULL;
    for(;;){
        pthread_mutex_lock(&G.lock);int waiting=pool_windows.waits>gate->previous_waits;pthread_mutex_unlock(&G.lock);
        if(waiting)break;
        if(dm_android_nvn_clock_ns()>deadline){gate->timed_out=1;break;}
        struct timespec pause={0,100000};nanosleep(&pause,NULL);
    }
    CHECK(vkSetEvent(G.device,gate->event));return NULL;
}
static void verify_async_upload(uint8_t *memory){
    uint8_t old[16],next[16];memset(old,0x59,sizeof old);memset(next,0xa7,sizeof next);
    memcpy(memory+4096,old,sizeof old);G.async_enabled=1;
    VkEvent event;VkEventCreateInfo info={.sType=VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};CHECK(vkCreateEvent(G.device,&info,NULL,&event));
    mtl_sub_begin();
    vkCmdWaitEvents(S.command,1,&event,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,NULL,0,NULL,0,NULL);
    VBuffer before=capture_bytes((uint64_t)(uintptr_t)memory+4096,16);mtl_sub_commit_async();
    /* The event keeps the old GPU read genuinely in flight. The helper only
     * releases it after the next host upload enters the ownership wait. */
    sub_begin_ctx(&G.submission_slot2);UploadGate gate={event,pool_windows.waits,0};pthread_t signaler;
    REQUIRE(!pthread_create(&signaler,NULL,release_upload_gate,&gate));
    memcpy(memory+4096,next,sizeof next);VBuffer after=capture_bytes((uint64_t)(uintptr_t)memory+4096,16);mtl_sub_commit();
    REQUIRE(!pthread_join(signaler,NULL)&&!gate.timed_out&&pool_windows.waits>gate.previous_waits);
    expect(&before,old,sizeof old);expect(&after,next,sizeof next);vkDestroyEvent(G.device,event,NULL);G.async_enabled=0;
}
typedef struct {VkEvent event;_Atomic int release;uint64_t backing_before;int wait_backing,timed_out;} VersionGate;
static void *release_version_gate(void *opaque){
    VersionGate *gate=opaque;uint64_t deadline=dm_android_nvn_clock_ns()+500000000ULL;
    for(;;){
        int release=atomic_load(&gate->release);
        if(gate->wait_backing){pthread_mutex_lock(&G.lock);release|=G.profile_backing_waits>gate->backing_before;pthread_mutex_unlock(&G.lock);}
        if(release)break;
        if(dm_android_nvn_clock_ns()>deadline){gate->timed_out=1;break;}
        struct timespec pause={0,100000};nanosleep(&pause,NULL);
    }
    CHECK(vkSetEvent(G.device,gate->event));return NULL;
}
static void verify_two_render_versions(uint8_t *memory){
    uint64_t address=(uint64_t)(uintptr_t)memory+4096;uint8_t old[16],next[16];
    memset(old,0x59,sizeof old);memset(next,0xa7,sizeof next);memcpy(memory+4096,old,sizeof old);
    G.render_slots=2;G.next_render_slot=0;G.async_enabled=1;
    VkEvent event;VkEventCreateInfo info={.sType=VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};CHECK(vkCreateEvent(G.device,&info,NULL,&event));
    VersionGate gate={.event=event};pthread_t signaler;REQUIRE(!pthread_create(&signaler,NULL,release_version_gate,&gate));
    mtl_sub_begin();REQUIRE(G.current_submission==&G.submission_slot);
    vkCmdWaitEvents(S.command,1,&event,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,NULL,0,NULL,0,NULL);
    VPool *v0=read_view(address,16);VkBuffer backing0=v0->v.b;
    VBuffer before=capture_view(v0,address,16);mtl_sub_commit_async();
    REQUIRE(vkGetFenceStatus(G.device,G.submission_slot.gpu_fence)==VK_NOT_READY);
    mtl_sub_begin();REQUIRE(G.current_submission==&G.submission_slot3);
    memcpy(memory+4096,next,sizeof next);VPool *v1=read_view(address,16);
    REQUIRE(v1->v.b!=backing0);VBuffer after=capture_view(v1,address,16);mtl_sub_commit_async();
    pthread_mutex_lock(&G.lock);REQUIRE(g_jcount==2&&ctx_busy(&G.submission_slot)&&ctx_busy(&G.submission_slot3));pthread_mutex_unlock(&G.lock);
    atomic_store(&gate.release,1);REQUIRE(!pthread_join(signaler,NULL)&&!gate.timed_out);mtl_finish();
    expect(&before,old,sizeof old);expect(&after,next,sizeof next);vkDestroyEvent(G.device,event,NULL);

    /* Small CPU-accessible pools retain shared dense backing. A later CPU
     * upload must wait for the older reader, rather than mutating that backing. */
    size_t dense_size=2*1024*1024;uint8_t *dense=mmap(NULL,dense_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    REQUIRE(dense!=MAP_FAILED);mtl_pool_add_flags(dense,dense_size,0);address=(uint64_t)(uintptr_t)dense+4096;
    memcpy(dense+4096,old,sizeof old);CHECK(vkCreateEvent(G.device,&info,NULL,&event));
    gate=(VersionGate){.event=event,.backing_before=G.profile_backing_waits,.wait_backing=1};
    REQUIRE(!pthread_create(&signaler,NULL,release_version_gate,&gate));
    mtl_sub_begin();vkCmdWaitEvents(S.command,1,&event,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,NULL,0,NULL,0,NULL);
    before=capture_bytes(address,16);mtl_sub_commit_async();mtl_sub_begin();
    memcpy(dense+4096,next,sizeof next);after=capture_bytes(address,16);mtl_sub_commit();
    REQUIRE(!pthread_join(signaler,NULL)&&!gate.timed_out&&G.profile_backing_waits>gate.backing_before);
    expect(&before,old,sizeof old);expect(&after,next,sizeof next);vkDestroyEvent(G.device,event,NULL);
    /* Hold resource retirement while both real GPU fences finish. Even if a
     * later GPU write supersedes shared backing, each staged output retains
     * its own bytes and FIFO events cannot become visible before publication. */
    CHECK(vkCreateEvent(G.device,&info,NULL,&event));_Atomic uint32_t published=0;uint64_t off=0;
    VPool *pool=pool_find(address,16,&off);REQUIRE(pool&&pool->v.b);
    mtl_sub_begin();VkSubmission *first=G.current_submission;
    vkCmdWaitEvents(S.command,1,&event,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,NULL,0,NULL,0,NULL);
    vkCmdFillBuffer(S.command,pool->v.b,off,16,0x59595959);hz_note(HZ_TRANSFER);mark_write(address,16);
    mtl_sub_event((volatile uint32_t *)&published,0,1);mtl_sub_commit_async();
    mtl_sub_begin();VkSubmission *second=G.current_submission;REQUIRE(first!=second);
    vkCmdFillBuffer(S.command,pool->v.b,off,16,0xa7a7a7a7);hz_note(HZ_TRANSFER);mark_write(address,16);
    mtl_sub_event((volatile uint32_t *)&published,0,2);mtl_sub_commit_async();
    pthread_mutex_lock(&G.lock);CHECK(vkSetEvent(G.device,event));
    VkFence fences[2]={first->gpu_fence,second->gpu_fence};CHECK(vkWaitForFences(G.device,2,fences,VK_TRUE,500000000ULL));
    REQUIRE(first->wb_staged&&second->wb_staged&&!atomic_load(&published));
    wb_stage_invalidate(first);wb_stage_invalidate(second);
    REQUIRE(!memcmp(first->wb_stage.map,old,16)&&!memcmp(second->wb_stage.map,next,16));
    pthread_mutex_unlock(&G.lock);mtl_finish();
    REQUIRE(atomic_load(&published)==2&&!memcmp((void *)(uintptr_t)address,next,16));vkDestroyEvent(G.device,event,NULL);
    mtl_pool_del(dense);REQUIRE(!munmap(dense,dense_size));G.async_enabled=0;
}
static void verify_temp_cache_idle(int indexed){
    if(!temp_cache.idle_frames)return;
    REQUIRE(temp_cache.idle_frames==300);
    while(G.nretired){VBuffer *b=retired_take(G.nretired-1);buffer_free(b);free(b);}
    G.temp_index_enabled=indexed;memset(&G.temp_index,0,sizeof G.temp_index);
    mtl_sub_begin();VBuffer *cold=temporary(64,VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    memset(cold->map,0x4a,64);mtl_sub_commit();REQUIRE(G.nretired==1&&G.retired_buffers[0]==cold);
    G.present_count+=299;mtl_sub_begin();mtl_sub_commit();REQUIRE(G.nretired==1);
    /* A matching hot reuse resets its age and retains zero-initialized tails. */
    mtl_sub_begin();VBuffer *hot=temporary(64,VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    REQUIRE(hot==cold&&!G.nretired);uint8_t zero[64]={0};REQUIRE(!memcmp(hot->map,zero,sizeof zero));mtl_sub_commit();
    G.present_count+=100;mtl_sub_begin();mtl_sub_commit();REQUIRE(G.nretired==1);
    uint64_t reclaimed=temp_cache.reclaimed,bytes=temp_cache.reclaimed_bytes;
    G.present_count+=201;mtl_sub_begin();mtl_sub_commit();
    REQUIRE(!G.nretired&&temp_cache.reclaimed==reclaimed+1&&temp_cache.reclaimed_bytes>bytes);

    /* Age advances while a real GPU event holds this command in flight.
     * Trimming the completed cache must not touch its temporary allocation. */
    G.async_enabled=1;VkEvent event;VkEventCreateInfo info={.sType=VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};CHECK(vkCreateEvent(G.device,&info,NULL,&event));
    mtl_sub_begin();VkSubmission *pending=G.current_submission;
    VBuffer *live=temporary(64,VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    vkCmdWaitEvents(S.command,1,&event,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,NULL,0,NULL,0,NULL);
    vkCmdFillBuffer(S.command,live->b,0,64,0x73737373);S.command_dirty=1;hz_note(HZ_TRANSFER);
    VBuffer out=buffer_create_kind(64,VK_BUFFER_USAGE_TRANSFER_DST_BIT,1,DM_VK_MEMORY_READBACK);all_barrier();
    VkBufferCopy copy={0,0,64};vkCmdCopyBuffer(S.command,live->b,out.b,1,&copy);mtl_sub_commit_async();
    pthread_mutex_lock(&G.lock);REQUIRE(vkGetFenceStatus(G.device,pending->gpu_fence)==VK_NOT_READY);
    G.present_count+=temp_cache.idle_frames+60;temporaries_trim();
    REQUIRE(!G.nretired&&pending->ntemporary==1&&pending->temporaries[0]==live);
    CHECK(vkSetEvent(G.device,event));pthread_mutex_unlock(&G.lock);mtl_finish();
    uint8_t expected[64];memset(expected,0x73,sizeof expected);expect(&out,expected,sizeof expected);
    REQUIRE(G.nretired==1&&G.retired_buffers[0]==live&&temp_cache.retired_frame[0]==G.present_count);
    vkDestroyEvent(G.device,event,NULL);G.async_enabled=0;
    G.present_count+=temp_cache.idle_frames+60;mtl_sub_begin();mtl_sub_commit();REQUIRE(!G.nretired&&!G.retired_bytes);
    REQUIRE(!G.temp_index_enabled||!G.temp_index.n);
}

static void *release_gpu_migration(void *opaque){
    struct timespec pause={0,120000000};nanosleep(&pause,NULL);
    CHECK(vkSetEvent(G.device,*(VkEvent *)opaque));return NULL;
}
static void gpu_fill(uint64_t address,uint32_t word){
    uint64_t offset=0;VPool *p=pool_require_gpu_write(address,16,&offset);
    all_barrier();vkCmdFillBuffer(S.command,p->v.b,offset,16,word);S.command_dirty=1;hz_note(HZ_TRANSFER);mark_write(address,16);
}
static void verify_gpu_write_tiles(unsigned slots){
    if(!pool_windows.gpu_writes)return;
    int shared=G.linux_shared_pools;uint64_t minimum=G.linux_shared_pool_min,maximum=G.linux_shared_pool_max;
    G.linux_shared_pools=1;G.linux_shared_pool_min=G.linux_shared_pool_max=32ULL*1024*1024;
    G.render_slots=slots;G.next_render_slot=0;
    const uint64_t logical=64ULL*1024*1024;
    uint8_t *memory=mmap(NULL,logical,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);REQUIRE(memory!=MAP_FAILED);
    uint64_t base=(uint64_t)(uintptr_t)memory,a=base+4096,b=base+POOL_GPU_TILE+4096;
    uint8_t old[16],next[16],cpu[16];memset(old,0x59,16);memset(next,0xa7,16);memset(cpu,0x3c,16);
    mtl_pool_add_flags(memory,logical,NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS);
    VPool *owner=pool_find(base,logical,NULL);REQUIRE(owner&&owner->windowed&&owner->gpu_windows&&!owner->v.b);
    uint64_t serial=owner->serial;
    mtl_sub_begin();gpu_fill(a,0x59595959);VPool *tile=read_view(a,16);VkBuffer backing=tile->v.b;
    VBuffer before=capture_view(tile,a,16);
    /* A second disjoint tile must not replay the first tile's unknown GPU
     * payload; the original tile must serve every overlapping read/write. */
    gpu_fill(b,0xa7a7a7a7);REQUIRE(read_view(b,16)->v.b!=backing&&read_view(a,16)->v.b==backing);
    queued_cpu_update(memory+48*1024*1024,a+4,cpu);
    VBuffer *aligned=temporary_named(16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,TEMP_ALIGNED);
    vkCmdFillBuffer(S.command,aligned->b,0,16,0xa7a7a7a7);S.command_dirty=1;hz_note(HZ_TRANSFER);
    uint64_t aligned_offset=0;pool_require_gpu_write(a+32,16,&aligned_offset);
    S.aligned[S.naligned++]=(AlignedBinding){{a+32,16},aligned,1};mark_write(a+32,16);aligned_writeback();
    VBuffer aligned_result=capture_bytes(a+32,16);
    VBuffer after=capture_bytes(a+4,16),other=capture_bytes(b,16);mtl_sub_commit();
    expect(&before,old,16);expect(&after,cpu,16);expect(&other,next,16);expect(&aligned_result,next,16);
    REQUIRE(!owner->v.b&&pool_windows.bytes==2*POOL_GPU_TILE&&!memcmp((void *)(uintptr_t)(a+4),cpu,16));
    mtl_sub_begin();REQUIRE(read_view(a,16)->v.b==backing);before=capture_bytes(a+4,16);mtl_sub_commit();expect(&before,cpu,16);

    /* Keep an older producer genuinely pending while the current recording
     * writes another tile, then force full migration with a crossing read.
     * This exercises retirement before changing the completion lookup. */
    VkEvent event=VK_NULL_HANDLE;pthread_t signaler;uint64_t wait_before=G.profile_drain_wait_ns;
    if(slots==2){
        G.async_enabled=1;VkEventCreateInfo info={.sType=VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};
        CHECK(vkCreateEvent(G.device,&info,NULL,&event));
        mtl_sub_begin();VkSubmission *pending=G.current_submission;
        vkCmdWaitEvents(S.command,1,&event,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,NULL,0,NULL,0,NULL);
        gpu_fill(a,0x59595959);mtl_sub_commit_async();mtl_sub_begin();
        REQUIRE(vkGetFenceStatus(G.device,pending->gpu_fence)==VK_NOT_READY);
        gpu_fill(a+64,0xa7a7a7a7);REQUIRE(!owner->v.b&&g_jcount&&read_view(a+64,16)->v.b==backing);
        REQUIRE(!pthread_create(&signaler,NULL,release_gpu_migration,&event));
    }else{
        /* One render slot serializes owner acquisition. Exercise current
         * recording migration here; the two-slot case owns the real older
         * in-flight producer test. Never hold an event behind that wait. */
        mtl_sub_begin();gpu_fill(a,0x59595959);gpu_fill(a+64,0xa7a7a7a7);
    }
    VPool *dense=read_view(base+POOL_GPU_TILE-16,32);
    REQUIRE(dense==owner&&owner->v.b&&owner->gpu_window_dense);
    if(slots==2){
        REQUIRE(G.profile_drain_wait_ns>wait_before+30000000ULL);
        REQUIRE(!pthread_join(signaler,NULL));
    }
    VBuffer migrated_a=capture_bytes(a,16),migrated_b=capture_bytes(b,16),migrated_same=capture_bytes(a+64,16);
    mtl_sub_commit();expect(&migrated_a,old,16);expect(&migrated_b,next,16);expect(&migrated_same,next,16);
    REQUIRE(!memcmp((void *)(uintptr_t)a,old,16)&&!memcmp((void *)(uintptr_t)b,next,16)&&!memcmp((void *)(uintptr_t)(a+64),next,16));
    if(event)vkDestroyEvent(G.device,event,NULL);G.async_enabled=0;
    G.present_count+=POOL_WINDOW_IDLE+1;mtl_sub_begin();mtl_sub_commit();REQUIRE(owner->v.b&&!pool_windows.bytes);
    mtl_pool_del(memory);REQUIRE(!pool_windows.head&&!pool_windows.bytes);

    /* Fresh identity, cross-tile queued CPU update and a cross-tile GPU copy
     * preserve exact bytes while selecting the sticky full fallback. */
    memcpy(memory+4096,cpu,16);mtl_pool_add_flags(memory,logical,NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS);
    owner=pool_find(base,logical,NULL);REQUIRE(owner&&owner->serial!=serial&&owner->gpu_windows&&!owner->v.b);
    mtl_sub_begin();before=capture_bytes(a,16);gpu_fill(b,0x59595959);
    queued_cpu_update(memory+48*1024*1024,base+2*POOL_GPU_TILE-8,cpu);
    REQUIRE(owner->v.b&&owner->gpu_window_dense);
    after=capture_bytes(base+2*POOL_GPU_TILE-8,16);other=capture_bytes(b,16);
    mtl_sub_copy(a,base+3*POOL_GPU_TILE-8,16);VBuffer copied=capture_bytes(base+3*POOL_GPU_TILE-8,16);
    mtl_sub_commit();expect(&before,cpu,16);expect(&after,cpu,16);expect(&other,old,16);expect(&copied,cpu,16);
    mtl_pool_del(memory);REQUIRE(!pool_windows.head&&!pool_windows.bytes&&!munmap(memory,logical));
    G.linux_shared_pools=shared;G.linux_shared_pool_min=minimum;G.linux_shared_pool_max=maximum;
    fprintf(stderr,"GPU_WRITE_TILES_OK render_slots=%u tile_bytes=%llu persistent_aliases=1 current_gpu_migration=1 older_fence_migration=%d crossing_cpu_update=1 guest_writeback=1 sticky_dense=1 address_reuse=1\n",slots,(unsigned long long)POOL_GPU_TILE,slots==2);
}

static void classic_transition(int active,VkEvent event) {
    pthread_t signaler;REQUIRE(!pthread_create(&signaler,NULL,release_gpu_migration,&event));
    uint64_t start=dm_android_nvn_clock_ns();pthread_mutex_lock(&G.lock);
    pool_window_classic_set(active);pthread_mutex_unlock(&G.lock);
    REQUIRE(dm_android_nvn_clock_ns()-start>30000000ULL);
    REQUIRE(!pthread_join(signaler,NULL));vkDestroyEvent(G.device,event,NULL);
}
static void verify_classic_transitions(unsigned slots) {
    int shared=G.linux_shared_pools;uint64_t minimum=G.linux_shared_pool_min,maximum=G.linux_shared_pool_max;
    G.linux_shared_pools=1;G.linux_shared_pool_min=G.linux_shared_pool_max=32ULL*1024*1024;
    G.render_slots=slots;G.next_render_slot=0;G.async_enabled=1;
    const uint64_t logical=64ULL*1024*1024;
    uint8_t *memory=mmap(NULL,logical,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    REQUIRE(memory!=MAP_FAILED);uint64_t base=(uint64_t)(uintptr_t)memory,a=base+4096,b=base+POOL_GPU_TILE+4096;
    uint8_t cpu[16],old[16],next[16],fresh[16];memset(cpu,0x3c,16);memset(old,0x59,16);memset(next,0xa7,16);memset(fresh,0x22,16);
    memcpy(memory+4096,cpu,16);mtl_pool_add_flags(memory,logical,NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS);
    VPool *owner=pool_find(base,logical,NULL);REQUIRE(owner&&!owner->windowed&&!owner->gpu_windows);
    uint64_t serial=owner->serial;VkEvent event;VkEventCreateInfo info={.sType=VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};
    CHECK(vkCreateEvent(G.device,&info,NULL,&event));mtl_sub_begin();
    VBuffer initial=capture_bytes(a,16);
    vkCmdWaitEvents(S.command,1,&event,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,NULL,0,NULL,0,NULL);
    gpu_fill(a,0x59595959);queued_cpu_update(memory+48*1024*1024,a+32,cpu);
    VkSubmission *pending=G.current_submission;mtl_sub_commit_async();
    REQUIRE(vkGetFenceStatus(G.device,pending->gpu_fence)==VK_NOT_READY&&owner->v.b);
    G.pool_uploads[0][0]=(PoolUpload){.pool_serial=serial,.stamp=123};
    classic_transition(1,event);expect(&initial,cpu,16);
    REQUIRE(owner->serial==serial&&owner->windowed&&owner->gpu_windows&&!owner->v.b&&!pool_windows.bytes);
    REQUIRE(!G.pool_uploads[0][0].pool_serial&&!memcmp((void *)(uintptr_t)a,old,16)&&!memcmp((void *)(uintptr_t)(a+32),cpu,16));
    /* Pools registered after admission must inherit the active residency mode. */
    uint8_t *late=mmap(NULL,logical,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);REQUIRE(late!=MAP_FAILED);
    mtl_pool_add_flags(late,logical,NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS);
    REQUIRE(pool_find((uint64_t)(uintptr_t)late,logical,NULL)->gpu_windows);
    mtl_pool_del(late);REQUIRE(!munmap(late,logical));
    mtl_sub_begin();VBuffer from_dense=capture_bytes(a,16),cpu_from_dense=capture_bytes(a+32,16);
    gpu_fill(b,0x59595959);VBuffer tile_output=capture_bytes(b,16);mtl_sub_commit();
    expect(&from_dense,old,16);expect(&cpu_from_dense,cpu,16);expect(&tile_output,old,16);
    REQUIRE(!owner->v.b&&pool_windows.bytes==2*POOL_GPU_TILE);
    CHECK(vkCreateEvent(G.device,&info,NULL,&event));mtl_sub_begin();
    vkCmdWaitEvents(S.command,1,&event,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,NULL,0,NULL,0,NULL);
    gpu_fill(a,0xa7a7a7a7);queued_cpu_update(memory+48*1024*1024,a+32,next);
    pending=G.current_submission;mtl_sub_commit_async();REQUIRE(vkGetFenceStatus(G.device,pending->gpu_fence)==VK_NOT_READY);
    classic_transition(0,event);
    REQUIRE(owner->serial==serial&&!owner->windowed&&!owner->gpu_windows&&!owner->gpu_window_dense&&!owner->v.b);
    REQUIRE(!pool_windows.head&&!pool_windows.bytes&&!memcmp((void *)(uintptr_t)a,next,16));
    mtl_sub_begin();VBuffer from_tiles=capture_bytes(a,16),cpu_from_tiles=capture_bytes(a+32,16),other=capture_bytes(b,16);mtl_sub_commit();
    expect(&from_tiles,next,16);expect(&cpu_from_tiles,next,16);expect(&other,old,16);REQUIRE(owner->v.b);
    memcpy((void *)(uintptr_t)b,fresh,16);mtl_sub_begin();other=capture_bytes(b,16);mtl_sub_commit();expect(&other,fresh,16);
    /* A dense cross-tile fallback must be releasable at a later scene boundary. */
    pthread_mutex_lock(&G.lock);pool_window_classic_set(1);pthread_mutex_unlock(&G.lock);
    mtl_sub_begin();read_view(base+POOL_GPU_TILE-16,32);REQUIRE(owner->v.b&&owner->gpu_window_dense);mtl_sub_commit();
    pthread_mutex_lock(&G.lock);pool_window_classic_set(0);pthread_mutex_unlock(&G.lock);
    REQUIRE(!owner->v.b&&!owner->gpu_window_dense&&!owner->gpu_windows&&!pool_windows.bytes);
    mtl_pool_del(memory);REQUIRE(!munmap(memory,logical));G.async_enabled=0;
    G.linux_shared_pools=shared;G.linux_shared_pool_min=minimum;G.linux_shared_pool_max=maximum;
    fprintf(stderr,"CLASSIC_GPU_WINDOWS_FIXTURE_OK render_slots=%u dense_to_tiles=1 tiles_to_dense=1 pending_fence=1 cpu_gpu_generations=1 late_pool=1 upload_invalidation=1 fallback_reset=1\n",slots);
}

int main(int argc,char **argv){
    REQUIRE(argc==2||argc==3);int dynamic=argc==2;
    unsetenv("DM_LINUX_SHARED_POOLS");unsetenv("DM_LINUX_SHARED_ASYNC");
    setenv("DM_DISABLE_ASYNC_COMMIT","1",1);
    if(dynamic)setenv("DM_LINUX_POOL_WINDOWS","1",1);else unsetenv("DM_LINUX_POOL_WINDOWS");
    dm_linux_memory_guard_init();crash_thread_init();REQUIRE(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS));
    probe_window=SDL_CreateWindow("D2R pool backing verification",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,64,64,
        SDL_WINDOW_SHOWN|(dm_linux_vulkan_private_requested()?0:SDL_WINDOW_VULKAN));
    REQUIRE(probe_window&&dm_linux_vulkan_bind_window(probe_window));dm_vk_set_window(probe_window);mtl_init();
    unsigned requested_render_slots=G.render_slots;G.render_slots=1;
    REQUIRE(!G.async_enabled&&!G.linux_shared_async_available&&pool_windows.enabled==dynamic);
    const uint64_t unit=pool_windows.granule,logical=64ULL*1024*1024;
    uint8_t *gpu_only=mmap(NULL,logical,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);REQUIRE(gpu_only!=MAP_FAILED);
    mtl_pool_add_flags(gpu_only,logical,NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS);
    REQUIRE(!pool_find((uint64_t)(uintptr_t)gpu_only,logical,NULL)->windowed);
    mtl_pool_del(gpu_only);REQUIRE(!munmap(gpu_only,logical));
    uint8_t *memory=mmap(NULL,logical,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);REQUIRE(memory!=MAP_FAILED);
    uint64_t base=(uint64_t)(uintptr_t)memory,cross=base+unit-16,source=base+10*1024*1024,destination=base+20*1024*1024;
    uint8_t initial[64],left[16],right[16],payload[16],second[16],updated[64];
    memset(initial,0x11,sizeof initial);memset(left,0x32,sizeof left);memset(right,0x41,sizeof right);
    for(unsigned i=0;i<16;i++){payload[i]=(uint8_t)(0xe0+i);second[i]=(uint8_t)(0x80+i);}
    memcpy((void *)(uintptr_t)cross,initial,64);memcpy(memory+4096,left,16);memcpy(memory+unit+4096,right,16);
    mtl_pool_add_flags(memory,logical,0);VPool *owner=pool_find(base,logical,NULL);REQUIRE(owner&&owner->windowed==dynamic);
    uint64_t first_serial=owner->serial;
    mtl_sub_begin();VPool *l=read_view(base+4096,16),*r=read_view(base+unit+4096,16);
    VBuffer first=capture_view(l,base+4096,16),other=capture_view(r,base+unit+4096,16);mtl_sub_commit();
    expect(&first,left,16);expect(&other,right,16);
    if(dynamic)REQUIRE(!owner->v.b&&pool_windows.bytes==2*unit);else REQUIRE(owner->v.b&&owner->v.size==logical);

    /* Three overlapping windows must see one ordered CPU-authored update.
     * The snapshot queued before it must still retain the old GPU bytes. */
    mtl_sub_begin();VPool *wide=read_view(cross,64);VBuffer before=capture_view(wide,cross,64);
    queued_cpu_update(memory+40*1024*1024,cross+8,payload);
    VBuffer updated_left=capture_view(l,cross+8,8),updated_right=capture_view(r,cross+16,8);
    VBuffer after=capture_view(wide,cross,64);
    VPool *new_wide=read_view(base,2*unit+64);VBuffer new_after=capture_view(new_wide,cross,64);
    mtl_sub_commit();memcpy(updated,initial,64);memcpy(updated+8,payload,16);
    expect(&before,initial,64);expect(&updated_left,payload,8);expect(&updated_right,payload+8,8);
    expect(&after,updated,64);expect(&new_after,updated,64);REQUIRE(!memcmp((void *)(uintptr_t)cross,updated,64));
    if(dynamic)REQUIRE(!owner->v.b&&pool_windows.created>=4);

    /* A later unknown GPU producer promotes the same logical pool. Earlier
     * CPU updates are replayed, readonly buffers remain alive through fence,
     * and the original writeback publishes both destination and source. */
    mtl_sub_begin();queued_cpu_update(memory+40*1024*1024,source,second);
    VPool *larger=read_view(base,source-base+unit);VBuffer replayed=capture_view(larger,source,16);
    mtl_sub_copy(source,destination,16);
    VBuffer src=capture_bytes(source,16),dst=capture_bytes(destination,16);mtl_sub_commit();
    expect(&replayed,second,16);expect(&src,second,16);expect(&dst,second,16);
    REQUIRE(!memcmp((void *)(uintptr_t)source,second,16)&&!memcmp((void *)(uintptr_t)destination,second,16));
    REQUIRE(owner->v.b);if(dynamic)REQUIRE(pool_windows.promoted==1&&!pool_windows.bytes);

    /* Synthetic time advances only in this fixture. Actual fence publication
     * is still required before either dense or readonly backing can retire. */
    G.present_count+=POOL_WINDOW_IDLE+1;mtl_sub_begin();mtl_sub_commit();
    if(dynamic)REQUIRE(!owner->v.b&&pool_windows.demoted==1&&!pool_windows.bytes);
    mtl_sub_begin();src=capture_bytes(source,16);dst=capture_bytes(destination,16);mtl_sub_commit();
    expect(&src,second,16);expect(&dst,second,16);
    if(dynamic)REQUIRE(!owner->v.b&&pool_windows.bytes==2*unit);
    G.present_count+=POOL_WINDOW_IDLE+1;mtl_sub_begin();mtl_sub_commit();
    if(dynamic)REQUIRE(!pool_windows.bytes);
    mtl_pool_del(memory);REQUIRE(!pool_windows.head&&!pool_windows.bytes);

    /* Reusing exactly the same guest address must not inherit stale mirrors. */
    memset(memory+4096,0x6b,16);mtl_pool_add_flags(memory,logical,0);
    owner=pool_find(base,logical,NULL);REQUIRE(owner&&owner->serial!=first_serial);
    mtl_sub_begin();first=capture_bytes(base+4096,16);mtl_sub_commit();memset(left,0x6b,16);expect(&first,left,16);
    if(dynamic){if(requested_render_slots==2)verify_two_render_versions(memory);else verify_async_upload(memory);}
    mtl_pool_del(memory);REQUIRE(!pool_windows.head&&!pool_windows.bytes);REQUIRE(!munmap(memory,logical));
    verify_temp_cache_idle(0);verify_temp_cache_idle(1);
    if(dynamic)verify_gpu_write_tiles(requested_render_slots);
    if(dynamic&&!pool_windows.gpu_writes)verify_classic_transitions(requested_render_slots);
    DmVkMemoryStats stats;REQUIRE(!dm_vk_memory_get_stats(&stats)&&!stats.bytes[DM_VK_MEMORY_POOL]);
    FILE *out=fopen(argv[1],"w");REQUIRE(out);
    fprintf(out,"{\"passed\":true,\"dynamic\":%s,\"checks\":%u,\"gpu_bytes_verified\":true,\"overlapping_windows\":true,\"queued_update_order\":true,\"new_window_replays_updates\":true,\"dense_gpu_write_fallback\":true,\"guest_writeback\":true,\"idle_reclamation\":%s,\"address_reuse\":true,\"async_upload_ownership\":%s,\"pool_bytes_after_release\":0,\"temp_cache_idle_frames\":%llu,\"temp_cache_reclaimed\":%llu,\"game_executed\":false}\n",
        dynamic?"true":"false",checks,dynamic?"true":"false",dynamic?"true":"false",(unsigned long long)temp_cache.idle_frames,(unsigned long long)temp_cache.reclaimed);REQUIRE(!fclose(out));
    printf("LINUX_POOL_WINDOW_OK dynamic=%d checks=%u actual_gpu_bytes=1 render_slots_tested=%u granule=%llu game_executed=0\n",dynamic,checks,requested_render_slots,(unsigned long long)unit);return 0;
}
