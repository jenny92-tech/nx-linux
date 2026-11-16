// INPUT:  Host-encoded ASTC fixtures and a native Linux Vulkan/SDL surface.
// OUTPUT: Byte-exact 4x4/8x8 uploads/copies and decoded GPU pixel checks.
// POS:    Exercise the production renderer without executing game code.
#include "../../host/hle.h"
#undef die
#include "../../gfx/nvn/nvn_vk.c"
#include "../memory_guard.h"
static SDL_Window *probe_window;
void crash_thread_init(void);
void *dm_android_native_window(void) { return probe_window; }
void dm_android_front_presented(uint64_t frame) { (void)frame; }
double front_display_refresh(char *name,size_t capacity) { snprintf(name,capacity,"ASTC fixture");return 60; }
void dm_host_shutdown(const char *why) { fprintf(stderr,"Unexpected shutdown: %s\n",why);_exit(2); }
void dm_shot_written(const char *path,int ok) { (void)path;(void)ok; }
static unsigned checks;
#define REQUIRE(x) do { checks++;if(!(x)){fprintf(stderr,"ASTC_FAIL line=%d expression=%s\n",__LINE__,#x);abort();} } while(0)
static void fixture_file(const char *root,const char *name,void *out,size_t bytes) {
    char path[2048];snprintf(path,sizeof path,"%s/%s",root,name);
    FILE *f=fopen(path,"rb");REQUIRE(f);REQUIRE(fread(out,1,bytes,f)==bytes);REQUIRE(fgetc(f)==EOF);fclose(f);
}
static tex_t make_texture(uint8_t *memory,unsigned fmt,uint64_t serial,unsigned width,unsigned height) {
    tex_t t={.d={.target=NVN_TEXTURE_TARGET_2D,.format=fmt,.width=width,.height=height,.depth=1,.levels=2},.mem=memory,.serial=serial};
    nvn_tex_layout(&t.d,&t.L);return t;
}
static void read_blocks(tex_t *source,unsigned level,uint8_t *out,size_t bytes) {
    mtl_sub_begin();VTexture *t=texture_get(source,1);texture_transition(t,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    unsigned w=source->d.width>>level,h=source->d.height>>level;if(!w)w=1;if(!h)h=1;
    VBuffer b=buffer_create_kind(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT,1,DM_VK_MEMORY_READBACK);
    VkBufferImageCopy c={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,1},.imageExtent={w,h,1}};
    vkCmdCopyImageToBuffer(S.command,t->image,t->layout,b.b,1,&c);S.submission_cpu_readback=1;mtl_sub_commit();
    memcpy(out,b.map,bytes);buffer_free(&b);
}
int main(int argc,char **argv) {
    REQUIRE(argc==3);dm_linux_memory_guard_init();crash_thread_init();
    REQUIRE(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS));
    probe_window=SDL_CreateWindow("D2R ASTC verification",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,64,64,
        SDL_WINDOW_SHOWN|(dm_linux_vulkan_private_requested()?0:SDL_WINDOW_VULKAN));
    REQUIRE(probe_window&&dm_linux_vulkan_bind_window(probe_window));
    dm_vk_set_window(probe_window);mtl_init();REQUIRE(G.enabled_features.textureCompressionASTC_LDR);
    uint8_t *memory;REQUIRE(!posix_memalign((void **)&memory,16384,262144));memset(memory,0,262144);mtl_pool_add(memory,262144);
    for(unsigned kind=0;kind<2;kind++)for(unsigned srgb=0;srgb<2;srgb++) {
        unsigned block=kind?8:4,width=2*block-1,height=block+1,index=2*kind+srgb;
        unsigned fmt=(srgb?NVN_FORMAT_RGBA_ASTC_4x4_SRGB:NVN_FORMAT_RGBA_ASTC_4x4)+(kind?7:0);
        tex_t source=make_texture(memory+index*32768,fmt,9000+index*10,width,height);
        tex_t other=make_texture(memory+index*32768+8192,fmt,9001+index*10,width,height);
        tex_t dst=make_texture(memory+index*32768+16384,srgb?NVN_FORMAT_RGBA_DXT5_SRGB:NVN_FORMAT_RGBA_DXT5,9002+index*10,width,height);
        uint8_t blocks[64],second[64],actual[64],golden[width*height*4],pixels[width*height*4];
        char name[64];snprintf(name,sizeof name,"%s%s.blocks",kind?"8x8-":"",srgb?"srgb":"linear");
        fixture_file(argv[1],name,blocks,sizeof blocks);
        snprintf(name,sizeof name,"%s%s.rgba",kind?"8x8-":"",srgb?"srgb":"linear");
        fixture_file(argv[1],name,golden,sizeof golden);
        memset(second,0,sizeof second);for(unsigned i=0;i<4;i++)memcpy(second+16*i,blocks+16*((i+1)%4),16);
        REQUIRE(!nvn_swizzle(&source.d,0,0,blocks,32,source.mem));
        REQUIRE(!nvn_swizzle(&source.d,1,0,blocks,16,source.mem));
        REQUIRE(!nvn_swizzle(&other.d,0,0,second,32,other.mem));
        REQUIRE(!nvn_swizzle(&other.d,1,0,second,16,other.mem));
        read_blocks(&source,0,actual,64);REQUIRE(!memcmp(actual,blocks,64));
        read_blocks(&source,1,actual,16);REQUIRE(!memcmp(actual,blocks,16));
        uint8_t padded[96];memset(padded,0x5a,sizeof padded);NVNcopyRegion region={.width=width,.height=height,.depth=1};
        mtl_read_texels(&source,0,0,&region,padded,48,96);
        REQUIRE(!memcmp(padded,blocks,32)&&!memcmp(padded+48,blocks+32,32));
        for(unsigned i=32;i<48;i++)REQUIRE(padded[i]==0x5a&&padded[i+48]==0x5a);
        mtl_sub_begin();p_copy_t2t copy={.src=&source,.dst=&dst,.sr={.width=width,.height=height,.depth=1},.dr={.width=width,.height=height,.depth=1}};
        copy_t2t(&copy);mtl_sub_commit();
        /* Read the actual RGBA allocation, because NVN still calls it BC3. */
        mtl_sub_begin();VTexture *d=texture_get(&dst,0);VBuffer read=buffer_create_kind(sizeof pixels,VK_BUFFER_USAGE_TRANSFER_DST_BIT,1,DM_VK_MEMORY_READBACK);
        texture_transition(d,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);VkBufferImageCopy c={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={width,height,1}};
        vkCmdCopyImageToBuffer(S.command,d->image,d->layout,read.b,1,&c);S.submission_cpu_readback=1;mtl_sub_commit();memcpy(pixels,read.map,sizeof pixels);buffer_free(&read);
        for(unsigned i=0;i<sizeof pixels;i++)REQUIRE(abs((int)pixels[i]-golden[i])<=1);
        /* GPU replaces the full base mip, then CPU changes only its first
         * compression block. Refresh must retain all other GPU-authored blocks. */
        mtl_sub_begin();copy=(p_copy_t2t){.src=&other,.dst=&source,.sr={.width=width,.height=height,.depth=1},.dr={.width=width,.height=height,.depth=1}};
        copy_t2t(&copy);mtl_sub_commit();uint8_t changed[64];memcpy(changed,blocks,64);memcpy(changed,blocks+32,16);
        dm_guest_write_prepare(source.mem,source.L.size);REQUIRE(!nvn_swizzle(&source.d,0,0,changed,32,source.mem));
        read_blocks(&source,0,actual,64);memcpy(second,changed,16);REQUIRE(!memcmp(actual,second,64));
        uint8_t *pitched=memory+196608;dm_guest_write_prepare(pitched,96);memset(pitched,0xa5,96);
        memcpy(pitched,blocks,32);memcpy(pitched+48,blocks+32,32);
        mtl_sub_begin();G.state.copy_stride=48;
        p_copy_b2t transfer={.src=(uint64_t)(uintptr_t)pitched,.tex=&source,.r={.width=width,.height=height,.depth=1}};
        copy_b2t(&transfer);G.state.copy_stride=0;mtl_sub_commit();
        read_blocks(&source,0,actual,64);REQUIRE(!memcmp(actual,blocks,64));
        read_blocks(&source,1,actual,16);REQUIRE(!memcmp(actual,blocks,16));
        mtl_tex_release(&source);mtl_tex_release(&other);mtl_tex_release(&dst);
    }
    mtl_pool_del(memory);free(memory);
    FILE *out=fopen(argv[2],"w");REQUIRE(out);fprintf(out,"{\"passed\":true,\"checks\":%u,\"native_astc_upload\":true,\"block_sizes\":[4,8],\"small_mips\":true,\"linear_and_srgb_decode\":true,\"mixed_cpu_gpu_blocks_preserved\":true,\"padded_row_buffer_copy\":true,\"game_entry_executed\":false}\n",checks);REQUIRE(!fclose(out));
    printf("LINUX_ASTC_NATIVE_OK checks=%u game_entry_executed=0\n",checks);return 0;
}
