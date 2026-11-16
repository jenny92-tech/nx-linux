// INPUT: Synthetic BC1/ASTC storage and an explicit Linux Vulkan surface.
// OUTPUT: GPU pixels/blocks for equal writes, changed bytes and GPU ownership.
// POS: Production upload/baseline regression; no game assets or guest execution.
#include "../../host/hle.h"
#undef die
#include "../../gfx/nvn/nvn_vk.c"
#include "../memory_guard.h"
static SDL_Window *probe_window;
void crash_thread_init(void);
void *dm_android_native_window(void){return probe_window;}
void dm_android_front_presented(uint64_t frame){(void)frame;}
double front_display_refresh(char *name,size_t size){snprintf(name,size,"Compressed upload fixture");return 60;}
void dm_host_shutdown(const char *why){fprintf(stderr,"Unexpected shutdown: %s\n",why);_exit(2);}
void dm_shot_written(const char *path,int ok){(void)path;(void)ok;}
static unsigned checks;
#define REQUIRE(x) do{checks++;if(!(x)){fprintf(stderr,"COMPRESSED_UPLOAD_FAIL line=%d expression=%s\n",__LINE__,#x);abort();}}while(0)
static tex_t source(uint8_t *memory,unsigned format,unsigned size,uint64_t serial){
    tex_t t={.d={.target=NVN_TEXTURE_TARGET_2D,.format=(int)format,.width=size,.height=size,.depth=1,.levels=1},.mem=memory,.serial=serial};
    nvn_tex_layout(&t.d,&t.L);REQUIRE(t.L.size&&t.L.size<=32768);return t;
}
static void bc1(uint8_t linear[32],uint16_t color){
    memset(linear,0,32);for(unsigned b=0;b<4;b++){linear[b*8]=(uint8_t)color;linear[b*8+1]=(uint8_t)(color>>8);}
}
static void write_blocks(tex_t *t,const uint8_t *linear,unsigned row){
    dm_guest_write_prepare(t->mem,t->L.size);REQUIRE(!nvn_swizzle(&t->d,0,0,linear,row,t->mem));
}
static VTexture *read_image(tex_t *source,uint8_t *out,size_t bytes){
    mtl_sub_begin();VTexture *t=texture_get(source,1);all_barrier();texture_transition(t,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VBuffer b=buffer_create_kind(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT,1,DM_VK_MEMORY_READBACK);
    VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={source->d.width,source->d.height,1}};
    vkCmdCopyImageToBuffer(S.command,t->image,t->layout,b.b,1,&copy);S.submission_cpu_readback=1;mtl_sub_commit();
    memcpy(out,b.map,bytes);buffer_free(&b);return t;
}
int main(int argc,char **argv){
    REQUIRE(argc==2);unsetenv("DM_LINUX_SHARED_POOLS");unsetenv("DM_LINUX_POOL_WINDOWS");unsetenv("DM_LINUX_SHARED_ASYNC");
    REQUIRE(getenv("DM_LINUX_SNAPSHOT_CACHE"));setenv("DM_PROFILE_DETAIL","1",1);setenv("DM_DISABLE_ASYNC_COMMIT","1",1);
    dm_linux_memory_guard_init();crash_thread_init();REQUIRE(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS));
    probe_window=SDL_CreateWindow("Compressed upload bytes",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,64,64,SDL_WINDOW_SHOWN|(dm_linux_vulkan_private_requested()?0:SDL_WINDOW_VULKAN));
    REQUIRE(probe_window&&dm_linux_vulkan_bind_window(probe_window));dm_vk_set_window(probe_window);mtl_init();
    uint8_t *memory=mmap(NULL,131072,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);REQUIRE(memory!=MAP_FAILED);
    nvn_dirty_add_range(memory,131072);mtl_pool_add_flags(memory,131072,0);
    tex_t a=source(memory,NVN_FORMAT_RGB_DXT1,8,1),b=source(memory+32768,NVN_FORMAT_RGB_DXT1,8,2);
    uint8_t linear[32],actual[256];bc1(linear,0xf800);write_blocks(&a,linear,16);
    VTexture *t=read_image(&a,actual,sizeof actual);
    /* BC1 endpoint0 is independently expected as exact RGB565 red. */
    for(unsigned i=0;i<64;i++)REQUIRE(actual[i*4]==255&&actual[i*4+1]==0&&actual[i*4+2]==0&&actual[i*4+3]==255);
    REQUIRE(t->cpu_owned&&t->cpu_snapshot_mapped);uint8_t *baseline=t->cpu_snapshot;
    uint64_t uploads=G.uploads,version=t->version,skips=G.profile_compressed_same_uploads;
    write_blocks(&a,linear,16);REQUIRE(nvn_dirty_since(a.mem,a.L.size,t->upload_stamp));
    REQUIRE(read_image(&a,actual,sizeof actual)==t);REQUIRE(G.uploads==uploads&&t->version==version&&t->cpu_snapshot==baseline);
    REQUIRE(G.profile_compressed_same_uploads==skips+1);
    for(unsigned i=0;i<64;i++)REQUIRE(actual[i*4]==255&&actual[i*4+1]==0&&actual[i*4+2]==0&&actual[i*4+3]==255);
    /* A genuine change must replace the full CPU-owned image. */
    bc1(linear,0x07e0);write_blocks(&a,linear,16);read_image(&a,actual,sizeof actual);REQUIRE(G.uploads==uploads+1&&t->version>version);
    for(unsigned i=0;i<64;i++)REQUIRE(actual[i*4]==0&&actual[i*4+1]==255&&actual[i*4+2]==0&&actual[i*4+3]==255);
    uint8_t blue[32];bc1(blue,0x001f);write_blocks(&b,blue,16);
    mtl_sub_begin();p_copy_t2t copy={.src=&b,.dst=&a,.sr={.width=8,.height=8,.depth=1},.dr={.width=8,.height=8,.depth=1}};
    copy_t2t(&copy);mtl_sub_commit();REQUIRE(!t->cpu_owned);skips=G.profile_compressed_same_uploads;
    /* Equal CPU bytes must not reset a subsequently GPU-written image. */
    write_blocks(&a,linear,16);read_image(&a,actual,sizeof actual);REQUIRE(G.profile_compressed_same_uploads==skips);
    for(unsigned i=0;i<64;i++)REQUIRE(actual[i*4]==0&&actual[i*4+1]==0&&actual[i*4+2]==255&&actual[i*4+3]==255);
    /* The existing mixed-owner path updates changed blocks and retains blue
     * GPU pixels everywhere the original CPU green blocks are unchanged. */
    linear[0]=0;linear[1]=0xf8;write_blocks(&a,linear,16);read_image(&a,actual,sizeof actual);
    for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++){
        unsigned at=(y*8+x)*4;int red=x<4&&y<4;
        REQUIRE(actual[at]==(red?255:0)&&actual[at+1]==0&&actual[at+2]==(red?0:255)&&actual[at+3]==255);
    }
    /* Native ASTC copies retain exact compressed blocks; this is not an ASTC
     * shader/decode pixel claim. The void-extent block encodes opaque red. */
    tex_t astc=source(memory+65536,0x79,4,3);uint8_t block[16]={0xfc,0xfd,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0,0,0,0,0xff,0xff},raw[16];
    REQUIRE(native_astc(astc.d.format));write_blocks(&astc,block,16);VTexture *at=read_image(&astc,raw,sizeof raw);REQUIRE(!memcmp(raw,block,16));
    uploads=G.uploads;version=at->version;baseline=at->cpu_snapshot;skips=G.profile_compressed_same_uploads;
    write_blocks(&astc,block,16);read_image(&astc,raw,sizeof raw);REQUIRE(!memcmp(raw,block,16)&&G.uploads==uploads&&at->version==version&&at->cpu_snapshot==baseline&&G.profile_compressed_same_uploads==skips+1);
    block[8]=0;block[9]=0;block[10]=0xff;block[11]=0xff;write_blocks(&astc,block,16);read_image(&astc,raw,sizeof raw);REQUIRE(!memcmp(raw,block,16)&&G.uploads==uploads+1);
    mtl_finish();mtl_pool_del(memory);nvn_dirty_remove_range(memory);REQUIRE(!munmap(memory,131072));
    FILE *out=fopen(argv[1],"w");REQUIRE(out);fprintf(out,"{\"passed\":true,\"checks\":%u,\"bc1_gpu_pixels\":true,\"unchanged_cpu_write\":true,\"changed_cpu_bytes\":true,\"gpu_written_image_preserved\":true,\"partial_cpu_change_preserves_gpu_pixels\":true,\"file_backed_baseline\":true,\"native_astc_block_roundtrip\":true,\"game_executed\":false}\n",checks);REQUIRE(!fclose(out));
    SDL_DestroyWindow(probe_window);SDL_Quit();return 0;
}
