/* G.lock protects this bounded ownership ledger. Released guest identities
 * disappear immediately; Vulkan objects survive every pre-existing slot.
 * Cache entries keep the entire exact image allocation (including its bound
 * memory), avoiding dedicated-allocation/rebinding assumptions. */
enum { TEXTURE_ALLOCATION_CACHE=48,TEXTURE_RETIRE_LIMIT=64 };
typedef struct TextureRetired {struct TextureRetired *next;VTexture texture;NvnTextureRetirement wait;uint64_t bytes;} TextureRetired;
static struct {
    uint64_t cache_limit,retire_limit,cached_bytes,pending_bytes;
    uint64_t hits,misses,queued,destroyed,fallbacks,trimmed;
    unsigned count,pending;TextureRetired *head;
    struct {VTexture texture;uint64_t frame;} cache[TEXTURE_ALLOCATION_CACHE];
} texture_lifetime;
static uint64_t texture_limit_mb(const char *name) {
    const char *s=getenv(name);if(!s)return 0;
    if(!*s||*s<'0'||*s>'9')die(name,0);
    char *end;errno=0;unsigned long long n=strtoull(s,&end,10);
    if(errno||*end||n>128)die(name,(long long)n);
    return n<<20;
}
static void texture_lifetime_config(void) {
    texture_lifetime.cache_limit=texture_limit_mb("DM_VK_IMAGE_CACHE_MB");
    texture_lifetime.retire_limit=texture_limit_mb("DM_VK_DEFER_TEXTURE_MB");
    if(texture_lifetime.cache_limit||texture_lifetime.retire_limit)
        fprintf(stderr,"[nvn-vulkan] TEXTURE_LIFETIME cache_limit=%llu retire_limit=%llu exact_image_reuse=1 real_publication=1\n",
            (unsigned long long)texture_lifetime.cache_limit,(unsigned long long)texture_lifetime.retire_limit);
}
static void texture_cache_drop(unsigned i) {
    VTexture *t=&texture_lifetime.cache[i].texture;
    vkDestroyImageView(G.device,t->view,NULL);vkDestroyImage(G.device,t->image,NULL);
    memory_release(t->memory,t->allocation_size,DM_VK_MEMORY_TEXTURE);
    texture_lifetime.cached_bytes-=t->allocation_size;texture_lifetime.trimmed++;
    texture_lifetime.cache[i]=texture_lifetime.cache[--texture_lifetime.count];
}
static uint64_t texture_cache_trim(int all) {
    uint64_t before=texture_lifetime.cached_bytes;
    for(unsigned i=0;i<texture_lifetime.count;){
        uint64_t frame=texture_lifetime.cache[i].frame,now=G.present_count;
        if(all||(now>=frame&&now-frame>=300))texture_cache_drop(i);else i++;
    }
    return before-texture_lifetime.cached_bytes;
}
static int texture_cache_headroom(uint64_t bytes) {
#ifdef DM_LINUX_HOST
    int dm_linux_memory_guard_optional(uint64_t);
    return dm_linux_memory_guard_optional(bytes);
#else
    (void)bytes;return 1;
#endif
}
/* Restore the old synchronous allocation headroom under pressure, while
 * retaining publication ordering. Called before mandatory allocation admission. */
static void texture_allocation_pressure(uint64_t bytes) {
    if((texture_lifetime.count||texture_lifetime.pending)&&!texture_cache_headroom(bytes)){
        texture_cache_trim(1);
        if(texture_lifetime.pending){texture_lifetime.fallbacks++;async_drain_locked();texture_cache_trim(1);}
    }
}
static int texture_allocation_take(const NvnTextureAllocationKey *key,VkImage *image,VkDeviceMemory *memory,VkImageView *view,uint64_t *bytes) {
    if(!texture_lifetime.cache_limit)return 0;
    texture_cache_trim(!texture_cache_headroom(0));
    for(unsigned i=0;i<texture_lifetime.count;i++){
        VTexture *t=&texture_lifetime.cache[i].texture;
        if(!nvn_texture_allocation_equal(key,&t->allocation_key))continue;
        *image=t->image;*memory=t->memory;*view=t->view;*bytes=t->allocation_size;
        texture_lifetime.cached_bytes-=*bytes;texture_lifetime.cache[i]=texture_lifetime.cache[--texture_lifetime.count];
        texture_lifetime.hits++;return 1;
    }
    texture_lifetime.misses++;return 0;
}
static void texture_allocation_give(VTexture *t) {
    uint64_t limit=texture_lifetime.cache_limit;
    if(!limit||!t->image||!t->allocation_size||t->allocation_size>limit||!texture_cache_headroom(0)){
        vkDestroyImageView(G.device,t->view,NULL);vkDestroyImage(G.device,t->image,NULL);
        memory_release(t->memory,t->allocation_size,DM_VK_MEMORY_TEXTURE);return;
    }
    while(texture_lifetime.count&&(texture_lifetime.count==TEXTURE_ALLOCATION_CACHE||texture_lifetime.cached_bytes>limit-t->allocation_size)){
        unsigned oldest=0;for(unsigned i=1;i<texture_lifetime.count;i++)if(texture_lifetime.cache[i].frame<texture_lifetime.cache[oldest].frame)oldest=i;
        texture_cache_drop(oldest);
    }
    unsigned at=texture_lifetime.count++;texture_lifetime.cache[at].texture=*t;
    texture_lifetime.cache[at].frame=G.present_count;texture_lifetime.cached_bytes+=t->allocation_size;
}
static void texture_destroy_retired(VTexture *t) {
    if(t->before_storage){vkDestroyImageView(G.device,t->before_storage_view,NULL);vkDestroyImage(G.device,t->before_storage,NULL);
        memory_release(t->before_storage_memory,t->before_storage_allocation_size,DM_VK_MEMORY_TEXTURE);G.image_retired_live--;}
    texture_snapshot_set(t,NULL,0);texture_derived_release(t);texture_allocation_give(t);texture_lifetime.destroyed++;
}
static void texture_retire_complete(VkSubmission *ctx) {
    if(!texture_lifetime.pending&&!texture_lifetime.count)return;
    VkSubmission *slots[3]={&G.submission_slot,&G.submission_slot2,&G.submission_slot3};unsigned slot=0;
    while(slot<3&&slots[slot]!=ctx)slot++;if(slot==3)die("texture retirement slot",0);
    TextureRetired **p=&texture_lifetime.head;
    while(*p){TextureRetired *r=*p;
        if(nvn_texture_retirement_done(&r->wait,slot,ctx->epoch))resource_cache_forget_texture(ctx,&r->texture);
        if(!nvn_texture_retirement_ready(&r->wait)){p=&r->next;continue;}
        *p=r->next;texture_lifetime.pending--;texture_lifetime.pending_bytes-=r->bytes;texture_destroy_retired(&r->texture);free(r);
    }
    if(texture_lifetime.count)texture_cache_trim(!texture_cache_headroom(0));
}
static int texture_retire_try(VTexture *t) {
    uint64_t bytes=t->allocation_size+t->before_storage_allocation_size+t->sample_allocation_size+t->shadow_allocation_size+t->cpu_snapshot_size;
    if(!texture_lifetime.retire_limit||bytes>texture_lifetime.retire_limit||texture_lifetime.pending_bytes>texture_lifetime.retire_limit-bytes||
       texture_lifetime.pending==TEXTURE_RETIRE_LIMIT)return 0;
    VkSubmission *slots[3]={&G.submission_slot,&G.submission_slot2,&G.submission_slot3};
    for(unsigned i=0;i<3;i++)if(slots[i]->phase==SUBMISSION_RECORDING)return 0;
    TextureRetired *r=calloc(1,sizeof *r);if(!r)return 0;
    r->texture=*t;r->bytes=bytes;
    for(unsigned i=0;i<3;i++){
        if(slots[i]->phase==SUBMISSION_UNUSED||slots[i]->phase==SUBMISSION_RETIRED)resource_cache_forget_texture(slots[i],t);
        else r->wait.epoch[i]=slots[i]->epoch;
    }
    texture_lifetime.queued++;
    if(nvn_texture_retirement_ready(&r->wait)){texture_destroy_retired(&r->texture);free(r);return 1;}
    r->next=texture_lifetime.head;texture_lifetime.head=r;texture_lifetime.pending++;texture_lifetime.pending_bytes+=bytes;return 1;
}
void dm_vk_texture_lifetime_stats(uint64_t out[8]) {
    pthread_mutex_lock(&G.lock);
    out[0]=texture_lifetime.cached_bytes;out[1]=texture_lifetime.pending_bytes;
    out[2]=texture_lifetime.hits;out[3]=texture_lifetime.misses;out[4]=texture_lifetime.queued;
    out[5]=texture_lifetime.destroyed;out[6]=texture_lifetime.fallbacks;out[7]=texture_lifetime.trimmed;
    pthread_mutex_unlock(&G.lock);
}
