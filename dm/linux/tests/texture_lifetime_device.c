/* Explicit real-device Vulkan fixture. Synthetic pixels only; never auto-run
 * during host checks, packaging or ordinary game startup. */
#include "../../host/hle.h"
#undef die
#include "../../gfx/nvn/nvn_vk.c"
#include "../memory_guard.h"
static SDL_Window *probe_window;
void crash_thread_init(void);
void *dm_android_native_window(void){return probe_window;}
void dm_android_front_presented(uint64_t frame){(void)frame;}
double front_display_refresh(char *name,size_t size){snprintf(name,size,"Texture lifetime fixture");return 60;}
void dm_host_shutdown(const char *why){fprintf(stderr,"Unexpected shutdown: %s\n",why);_exit(2);}
void dm_shot_written(const char *path,int ok){(void)path;(void)ok;}
static unsigned checks;
#define REQUIRE(x) do{checks++;if(!(x)){fprintf(stderr,"TEXTURE_LIFETIME_FAIL line=%d expression=%s\n",__LINE__,#x);abort();}}while(0)
typedef struct {VkEvent event;_Atomic int release;int timed_out;} Gate;
static void *release_gate(void *opaque){
    Gate *g=opaque;uint64_t end=dm_android_nvn_clock_ns()+1000000000ULL;
    while(!atomic_load(&g->release)){
        if(dm_android_nvn_clock_ns()>end){g->timed_out=1;break;}
        struct timespec pause={0,100000};nanosleep(&pause,NULL);
    }
    CHECK(vkSetEvent(G.device,g->event));return NULL;
}
static tex_t source(uint8_t *memory,uint64_t serial){
    tex_t t={.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=32,.height=32,.depth=1,.levels=1},.mem=memory,.serial=serial};
    nvn_tex_layout(&t.d,&t.L);REQUIRE(t.L.size&&t.L.size<=32768);return t;
}
static void pixels(tex_t *t,uint8_t red,uint8_t green){
    uint8_t raw[32*32*4];for(unsigned i=0;i<32*32;i++){raw[4*i]=red;raw[4*i+1]=green;raw[4*i+2]=0;raw[4*i+3]=255;}
    dm_guest_write_prepare(t->mem,t->L.size);REQUIRE(!nvn_swizzle(&t->d,0,0,raw,32*4,t->mem));
}
static void expect_pixels(tex_t *source,uint8_t red,uint8_t green){
    mtl_sub_begin();VTexture *t=texture_get(source,1);all_barrier();texture_transition(t,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VBuffer b=buffer_create_kind(32*32*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT,1,DM_VK_MEMORY_READBACK);
    VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={32,32,1}};
    vkCmdCopyImageToBuffer(S.command,t->image,t->layout,b.b,1,&copy);S.submission_cpu_readback=1;mtl_sub_commit();
    for(unsigned i=0;i<32*32;i++)REQUIRE(b.map[4*i]==red&&b.map[4*i+1]==green&&b.map[4*i+2]==0&&b.map[4*i+3]==255);
    buffer_free(&b);
}
int main(int argc,char **argv){
    REQUIRE(argc==2);unsetenv("DM_LINUX_SHARED_POOLS");unsetenv("DM_LINUX_POOL_WINDOWS");unsetenv("DM_LINUX_SHARED_ASYNC");
    setenv("DM_VK_IMAGE_CACHE_MB","16",1);setenv("DM_VK_DEFER_TEXTURE_MB","16",1);
    setenv("DM_RENDER_SLOTS","1",1);setenv("DM_DISABLE_ASYNC_COMMIT","1",1);dm_linux_memory_guard_init();crash_thread_init();REQUIRE(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS));
    probe_window=SDL_CreateWindow("Texture lifetimes",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,64,64,SDL_WINDOW_SHOWN|(dm_linux_vulkan_private_requested()?0:SDL_WINDOW_VULKAN));
    int bound=probe_window?dm_linux_vulkan_bind_window(probe_window):0;
    if(!bound)fprintf(stderr,"TEXTURE_FIXTURE_WINDOW window=%p error=%s\n",(void *)probe_window,SDL_GetError());
    REQUIRE(bound);dm_vk_set_window(probe_window);mtl_init();G.image_usage_by_flags=1;
    fprintf(stderr,"TEXTURE_FIXTURE_STAGE initialized slots=%d\n",G.render_slots);
    uint8_t *memory=mmap(NULL,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);REQUIRE(memory!=MAP_FAILED);
    nvn_dirty_add_range(memory,65536);mtl_pool_add_flags(memory,65536,0);tex_t a=source(memory,1);pixels(&a,255,0);
    fprintf(stderr,"TEXTURE_FIXTURE_STAGE pool_ready\n");
    VkEvent event;VkEventCreateInfo event_info={.sType=VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};CHECK(vkCreateEvent(G.device,&event_info,NULL,&event));
    Gate gate={.event=event};pthread_t signaler;REQUIRE(!pthread_create(&signaler,NULL,release_gate,&gate));
    fprintf(stderr,"TEXTURE_FIXTURE_STAGE begin_slot1\n");
    G.async_enabled=1;sub_begin_ctx(&G.submission_slot);VTexture *t=texture_get(&a,1);TextureRef old=texture_reference(t);VkImage old_image=t->image;
    vkCmdWaitEvents(S.command,1,&event,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,NULL,0,NULL,0,NULL);
    texture_transition(t,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);VkClearColorValue clear={{1.f,0.f,0.f,1.f}};VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdClearColorImage(S.command,t->image,t->layout,&clear,1,&range);mtl_sub_commit_async();
    REQUIRE(vkGetFenceStatus(G.device,G.submission_slot.gpu_fence)==VK_NOT_READY);uint64_t begin=dm_android_nvn_clock_ns();mtl_tex_release(&a);
    REQUIRE(dm_android_nvn_clock_ns()-begin<500000000ULL&&!texture_reference_get(old)&&texture_lifetime.pending==1&&!texture_lifetime.count);
    /* The same guest serial can be recreated while its old incarnation runs. */
    /* Slot 2 is initialized even with one render slot. Slot 3 exists only
     * with two render slots, which separately requires versioned pool windows. */
    pixels(&a,0,255);sub_begin_ctx(&G.submission_slot2);t=texture_get(&a,1);REQUIRE(t->image!=old_image&&t->incarnation!=old.incarnation);mtl_sub_commit_async();
    atomic_store(&gate.release,1);REQUIRE(!pthread_join(signaler,NULL)&&!gate.timed_out);mtl_finish();G.async_enabled=0;
    REQUIRE(!texture_lifetime.pending&&texture_lifetime.count==1);vkDestroyEvent(G.device,event,NULL);expect_pixels(&a,0,255);
    mtl_tex_release(&a);uint64_t hits=texture_lifetime.hits;pixels(&a,255,0);expect_pixels(&a,255,0);REQUIRE(texture_lifetime.hits==hits+1);
    /* Promotion must remember the new STORAGE allocation identity. */
    mtl_sub_begin();t=texture_get(&a,1);texture_require_storage(t);REQUIRE(t->allocation_key.usage&VK_IMAGE_USAGE_STORAGE_BIT);mtl_sub_commit();mtl_tex_release(&a);
    pthread_mutex_lock(&G.lock);texture_cache_trim(1);REQUIRE(!texture_lifetime.count&&!texture_lifetime.pending);pthread_mutex_unlock(&G.lock);
    mtl_pool_del(memory);nvn_dirty_remove_range(memory);REQUIRE(!munmap(memory,65536));
    FILE *out=fopen(argv[1],"w");REQUIRE(out);fprintf(out,"{\"passed\":true,\"checks\":%u,\"real_fence\":true,\"same_serial_incarnation\":true,\"reused_pixels\":true,\"storage_key\":true,\"game_executed\":false}\n",checks);REQUIRE(!fclose(out));
    SDL_DestroyWindow(probe_window);SDL_Quit();return 0;
}
