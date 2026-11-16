// INPUT: Logical NVN pools, exact reads/CPU updates and completed GPU epochs.
// OUTPUT: Readonly views or fixed GPU read/write tiles, with dense fallback.
// POS: Opt-in Linux residency cache, reclaimed only after real fences and effects.
#define POOL_WINDOW_BUDGET (128ULL*1024*1024)
#define POOL_WINDOW_IDLE 60ULL
#define POOL_GPU_TILE (pool_windows.gpu_granule)

static void pool_window_acquire(VPool *owner) {
    uint64_t previous=owner->last_use_epoch;owner->last_use_epoch=S.epoch;
    if(!owner->windowed||!previous||previous==S.epoch)return;
    /* A mapped CPU upload must not overwrite bytes still read by an older
     * GPU submission. Pin this owner to the recording epoch before releasing
     * G.lock: completion may trim other windows while this thread waits. */
    const VkSubmission *slots[]={&G.submission_slot,&G.submission_slot2,&G.submission_slot3};
    for(unsigned i=0;i<sizeof slots/sizeof slots[0];i++){
        const VkSubmission *sub=slots[i];
        if(sub->epoch!=previous||sub->phase==SUBMISSION_UNUSED||sub->phase==SUBMISSION_RETIRED)continue;
        if(S.phase!=SUBMISSION_RECORDING||G.completing)die("window host upload ownership",previous);
        pool_windows.waits++;async_drain_locked();break;
    }
}
static int pool_window_overlap(uint64_t a,uint64_t an,uint64_t b,uint64_t bn) {
    return a<b+bn&&b<a+an;
}
static void pool_window_record_cpu(VPool *target,Range write,const uint8_t *payload) {
    uint64_t base=(uint64_t)(uintptr_t)target->guest;
    if(!pool_window_overlap(base,target->size,write.addr,write.size))return;
    if(!payload)die("window promotion encountered an unmaterialized GPU producer",write.addr);
    uint64_t lo=base>write.addr?base:write.addr;
    uint64_t hi=base+target->size<write.addr+write.size?base+target->size:write.addr+write.size;
    if((lo-base)%4||(hi-lo)%4||hi-lo>65536)die("window CPU update alignment",lo);
    pass_end_at(__LINE__);
    vkCmdUpdateBuffer(S.command,target->v.b,lo-base,hi-lo,payload+(lo-write.addr));
    VkBufferMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .buffer=target->v.b,.offset=lo-base,.size=hi-lo};
    vkCmdPipelineBarrier(S.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0,0,NULL,1,&barrier,0,NULL);
}
static void pool_window_replay_cpu(VPool *target) {
    for(int i=0;i<S.nwrites;i++)pool_window_record_cpu(target,S.writes[i],S.write_cpu[i]);
}
static PoolWindow *pool_window_cached(VPool *owner,uint64_t addr,uint64_t bytes) {
    PoolWindow *best=NULL;
    uint64_t offset=addr-(uint64_t)(uintptr_t)owner->guest;
    for(PoolWindow *w=pool_windows.head;w;w=w->next){
        uint64_t start=(uint64_t)(uintptr_t)w->pool.guest-(uint64_t)(uintptr_t)owner->guest;
        if(w->owner_serial==owner->serial&&w->render_version==pool_window_owner_version(owner)&&nvn_vk_pool_window_contains(start,w->pool.size,offset,bytes)&&
           (!best||w->pool.size<best->pool.size))best=w;
    }
    return best;
}
static VPool *pool_resident_find(uint64_t addr,uint64_t bytes,uint64_t *offset) {
    VPool *owner=pool_find(addr,bytes,offset);if(!owner)return NULL;
    if(owner->v.b)return owner;
    if(owner->windowed){
        PoolWindow *w=pool_window_cached(owner,addr,bytes);
        if(w){if(offset)*offset=addr-(uint64_t)(uintptr_t)w->pool.guest;return &w->pool;}
    }
    return owner;
}
static VPool *pool_window_read_view(VPool *owner,uint64_t addr,uint64_t bytes,uint64_t *offset) {
    if(G.render_slots<2||!owner->windowed||owner->v.b)pool_window_acquire(owner);
    else owner->last_use_epoch=S.epoch;
    if(!owner->windowed||owner->v.b||!bytes)return owner;
    PoolWindow *w=pool_window_cached(owner,addr,bytes);
    if(!w){
        uint64_t start,size;
        if(!nvn_vk_pool_window_bounds(owner->size,addr-(uint64_t)(uintptr_t)owner->guest,bytes,
                owner->gpu_windows?POOL_GPU_TILE:pool_windows.granule,&start,&size))die("readonly pool window bounds",addr);
        if(owner->gpu_windows&&size>POOL_GPU_TILE){
            pool_materialize(owner);*offset=addr-(uint64_t)(uintptr_t)owner->guest;return owner;
        }
        w=calloc(1,sizeof *w);if(!w)die("readonly pool window allocation",size);
        w->owner_serial=owner->serial;w->render_version=pool_window_owner_version(owner);
        w->pool=(VPool){.guest=owner->guest+start,.size=size,.serial=++G.pool_adds};
        pool_materialize(&w->pool);
        /* CPU writes already queued for another overlapping view must retain
         * their exact ordering. Uploads still omit every queued write range. */
        pool_window_replay_cpu(&w->pool);
        w->next=pool_windows.head;pool_windows.head=w;
        pool_windows.bytes+=w->pool.v.allocation_size;pool_windows.created++;G.pool_revision++;
        if(pool_windows.bytes>pool_windows.peak_bytes)pool_windows.peak_bytes=pool_windows.bytes;
    }
    w->last_epoch=S.epoch;w->last_frame=G.present_count;
    *offset=addr-(uint64_t)(uintptr_t)w->pool.guest;return &w->pool;
}
static VPool *pool_require_gpu_write(uint64_t addr,uint64_t bytes,uint64_t *offset) {
    uniform_flush_range(addr,bytes);
    pool_output_wait(addr,bytes);
    VPool *owner=pool_find(addr,bytes,offset);if(!owner)die("unmapped GPU write range",addr);
    owner->last_gpu_write_frame=G.present_count+1;
    VPool *resident=owner;
    if(owner->gpu_windows&&!owner->v.b)resident=pool_window_read_view(owner,addr,bytes,offset);
    else pool_materialize(owner);
    backing_read_note(resident,*offset,bytes);return resident;
}
static int pool_window_update_cpu(uint64_t addr,uint64_t bytes,const uint8_t *payload) {
    VPool *owner=pool_find(addr,bytes,NULL);
    if(!owner||!owner->windowed||owner->v.b)return 0;
    uint64_t offset=0;pool_window_read_view(owner,addr,bytes,&offset);
    if(owner->v.b)return 0;
    for(PoolWindow *w=pool_windows.head;w;w=w->next)if(w->owner_serial==owner->serial&&w->render_version==pool_window_owner_version(owner)){
        if(pool_window_overlap((uint64_t)(uintptr_t)w->pool.guest,w->pool.size,addr,bytes)){
            pool_window_record_cpu(&w->pool,(Range){addr,bytes},payload);
            w->last_epoch=S.epoch;w->last_frame=G.present_count;
        }
    }
    return 1;
}
static void pool_window_free(PoolWindow **link) {
    PoolWindow *w=*link;*link=w->next;
    if(pool_windows.bytes<w->pool.v.allocation_size)die("window residency accounting",w->pool.serial);
    pool_windows.bytes-=w->pool.v.allocation_size;buffer_free(&w->pool.v);free(w);
    pool_windows.reclaimed++;G.pool_revision++;
}
static void pool_window_delete_owner(uint64_t serial) {
    PoolWindow **link=&pool_windows.head;
    while(*link){if((*link)->owner_serial==serial)pool_window_free(link);else link=&(*link)->next;}
}
/* Change backing only between render submissions, after every producer has
 * published its CPU mirror. Rebuilding from guest bytes while a fence is still
 * pending would discard GPU output or upload an older generation. */
static void pool_window_classic_set(int active) {
    if(active==pool_windows.classic_active)return;
    if(G.active)die("classic pool transition during recording",G.recording_epoch);
    async_drain_locked();
    for(unsigned i=0;i<3;i++){
        VkSubmission *ctx=submission_context(i);
        if(ctx->phase!=SUBMISSION_UNUSED&&ctx->phase!=SUBMISSION_RETIRED)
            die("classic pool transition before publication",ctx->epoch);
    }
    uint64_t released=0;unsigned changed=0;
    for(int i=0;i<G.npool;i++){
        VPool *owner=&G.pools[i];
        if(!pool_gpu_window_eligible(owner->size,owner->flags))continue;
        released+=owner->v.allocation_size;pool_window_delete_owner(owner->serial);
        buffer_free(&owner->v);
        for(unsigned bucket=0;bucket<UPLOAD_BUCKETS;bucket++)for(unsigned way=0;way<UPLOAD_WAYS;way++)
            if(G.pool_uploads[bucket][way].pool_serial==owner->serial)G.pool_uploads[bucket][way]=(PoolUpload){0};
        owner->last_use_epoch=owner->last_gpu_write_frame=0;
        owner->windowed=owner->gpu_windows=active;owner->gpu_window_dense=0;changed++;
    }
    pool_windows.classic_active=active;G.pool_revision++;G.registry_revision++;
    fprintf(stderr,"[nvn-vulkan] CLASSIC_GPU_WINDOWS mode=%s frame=%llu changed_pools=%u released_dense_bytes=%llu fenced=1\n",
        active?"tiles":"dense",(unsigned long long)G.present_count,changed,(unsigned long long)released);
}
#ifdef DM_LINUX_HOST
static void pool_window_classic_poll(void) {
    if(!pool_windows.classic_gpu)return;
    int gameplay=dm_linux_classic_gameplay_context();
    if(!gameplay)pool_windows.world_ready=0;
    pool_window_classic_set(gameplay&&pool_windows.world_ready);
}
#endif
static void pool_window_trim(uint64_t completed_epoch) {
    if(!pool_windows.enabled)return;
    if(S.phase!=SUBMISSION_PUBLISHED)
        die("window retirement without real fence publication",completed_epoch);
    /* Buffer views and submission temporaries were already retired. None of
     * these objects can be reused by an unfinished command buffer. */
    PoolWindow **link=&pool_windows.head;
    while(*link){
        PoolWindow *w=*link;VPool *owner=pool_find((uint64_t)(uintptr_t)w->pool.guest,w->pool.size,NULL);
        int stale=owner&&owner->serial==w->owner_serial&&owner->v.b;
        int old=G.present_count>=w->last_frame&&G.present_count-w->last_frame>=POOL_WINDOW_IDLE;
        int budget=pool_windows.bytes>POOL_WINDOW_BUDGET&&w->last_epoch<completed_epoch;
        if(w->last_epoch<=completed_epoch&&(stale||old||budget))pool_window_free(link);else link=&w->next;
    }
    for(int i=0;i<G.npool;i++){
        VPool *owner=&G.pools[i];
        if(!owner->windowed||owner->gpu_window_dense||!owner->v.b||owner->last_use_epoch>completed_epoch||
           G.present_count+1<owner->last_gpu_write_frame||
           G.present_count+1-owner->last_gpu_write_frame<POOL_WINDOW_IDLE)continue;
        buffer_free(&owner->v);
        /* Original logical identity survives; upload stamps describe a
         * deleted backing and must not survive a later dense promotion. */
        for(unsigned bucket=0;bucket<UPLOAD_BUCKETS;bucket++)for(unsigned way=0;way<UPLOAD_WAYS;way++)
            if(G.pool_uploads[bucket][way].pool_serial==owner->serial)G.pool_uploads[bucket][way]=(PoolUpload){0};
        G.pool_revision++;pool_windows.demoted++;
    }
    if(G.present_count-pool_windows.last_profile>=60){
        uint64_t logical=0,dense=0;unsigned nodes=0;
        for(int i=0;i<G.npool;i++){logical+=G.pools[i].size;dense+=G.pools[i].v.allocation_size;}
        for(PoolWindow *w=pool_windows.head;w;w=w->next)nodes++;
        fprintf(stderr,"[nvn-vulkan] POOL_WINDOW_PROFILE frame=%llu logical=%llu dense=%llu window_bytes=%llu windows=%u peak_window_bytes=%llu created=%llu reclaimed=%llu promoted=%llu demoted=%llu upload_waits=%llu async=%d\n",
            (unsigned long long)G.present_count,(unsigned long long)logical,(unsigned long long)dense,
            (unsigned long long)pool_windows.bytes,nodes,(unsigned long long)pool_windows.peak_bytes,
            (unsigned long long)pool_windows.created,(unsigned long long)pool_windows.reclaimed,
            (unsigned long long)pool_windows.promoted,(unsigned long long)pool_windows.demoted,
            (unsigned long long)pool_windows.waits,(int)G.async_enabled);
        pool_windows.last_profile=G.present_count;
    }
}
