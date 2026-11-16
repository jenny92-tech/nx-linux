/* Exercise the existing Linux readonly-pool window implementation on Android's
 * actual Vulkan device. No original game, Activity, resources or save is run. */
#include "hle.h"
#undef die
#include "../../gfx/nvn/nvn_vk.c"
#include <sys/mman.h>
struct dm_flags g_flag;uint64_t g_t0_ns;
void *dm_android_native_window(void){return NULL;}
extern void crash_thread_init(void);
static unsigned checks;
#define REQUIRE(x) do{checks++;if(!(x)){fprintf(stderr,"ANDROID_POOL_WINDOW_FAIL line=%d expression=%s\n",__LINE__,#x);abort();}}while(0)
static VBuffer capture_view(VPool *view,uint64_t address,uint64_t bytes){
    REQUIRE(address>=(uint64_t)(uintptr_t)view->guest&&bytes<=view->size-(address-(uint64_t)(uintptr_t)view->guest));
    VBuffer out=buffer_create_kind(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT,1,DM_VK_MEMORY_READBACK);
    all_barrier();VkBufferCopy copy={address-(uint64_t)(uintptr_t)view->guest,0,bytes};
    vkCmdCopyBuffer(S.command,view->v.b,out.b,1,&copy);return out;
}
static VPool *read_view(uint64_t address,uint64_t bytes){uint64_t offset=0;return pool_read(address,bytes,&offset);}
static VBuffer capture_bytes(uint64_t address,uint64_t bytes){return capture_view(read_view(address,bytes),address,bytes);}
static void expect(VBuffer *actual,const void *expected,size_t bytes){REQUIRE(!memcmp(actual->map,expected,bytes));buffer_free(actual);}
static void queued_update(uint8_t *command_memory,uint64_t address,const uint8_t payload[16]){
    struct {op_hdr head;uint64_t address,bytes;uint8_t payload[16];op_hdr end;} command={
        .head={OP_UPDATE_UB,40},.address=address,.bytes=16,.end={OP_END,8}};
    memcpy(command.payload,payload,16);memcpy(command_memory,&command,sizeof command);mtl_exec(command_memory);
}
int main(int argc,char **argv){
    REQUIRE(!setenv("DM_LINUX_POOL_WINDOWS","1",1));
    REQUIRE(!setenv("DM_DISABLE_ASYNC_COMMIT","1",1));
    crash_thread_init();mtl_init();g_t0_ns=dm_android_nvn_clock_ns();
    if(argc==2){
        dm_vk_set_shader_root(argv[1]);size_t bytes=0;uint8_t *index=read_index_mapping(&bytes);
        REQUIRE(index&&bytes>=12);uint32_t header[3];memcpy(header,index,12);
        REQUIRE(header[0]==0x494b5644&&header[1]==2&&header[2]>0);size_t cursor=12;
        nvn_shader_index_record first,record;
        REQUIRE(nvn_shader_index_read(index,bytes,header[1],&cursor,&first));
        for(uint32_t i=1;i<header[2];i++)REQUIRE(nvn_shader_index_read(index,bytes,header[1],&cursor,&record));
        REQUIRE(cursor==bytes);uint8_t control[0x880];memcpy(control,first.control,sizeof control);
        uint8_t *code=malloc(first.code_size);REQUIRE(code&&first.code_size);memcpy(code,first.code,first.code_size);
        REQUIRE(!madvise(index,bytes,MADV_DONTNEED));
        REQUIRE(nvn_shader_index_matches_live(first.entry,control,first.stage,code,first.code_size));
        free(code);REQUIRE(!munmap(index,bytes));
        printf("ANDROID_SHADER_INDEX_MAP_OK bytes=%zu records=%u retained_identity_after_reclaim=1\n",bytes,header[2]);
    }else REQUIRE(argc==1);
    REQUIRE(pool_windows.enabled&&!G.linux_shared_pools&&!G.async_enabled&&G.render_slots==1);
    const uint64_t unit=pool_windows.granule,logical=64ULL*1024*1024;
    uint8_t *memory=mmap(NULL,logical,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);REQUIRE(memory!=MAP_FAILED);
    uint64_t base=(uint64_t)(uintptr_t)memory,cross=base+unit-16,source=base+10*1024*1024,destination=base+20*1024*1024;
    uint8_t initial[64],left[16],right[16],payload[16],second[16],updated[64];
    memset(initial,0x11,sizeof initial);memset(left,0x32,sizeof left);memset(right,0x41,sizeof right);
    for(unsigned i=0;i<16;i++){payload[i]=(uint8_t)(0xe0+i);second[i]=(uint8_t)(0x80+i);}
    memcpy((void *)(uintptr_t)cross,initial,64);memcpy(memory+4096,left,16);memcpy(memory+unit+4096,right,16);
    mtl_pool_add_flags(memory,logical,0);VPool *owner=pool_find(base,logical,NULL);REQUIRE(owner&&owner->windowed);
    uint64_t serial=owner->serial;
    mtl_sub_begin();VPool *l=read_view(base+4096,16),*r=read_view(base+unit+4096,16);
    VBuffer first=capture_view(l,base+4096,16),other=capture_view(r,base+unit+4096,16);mtl_sub_commit();
    expect(&first,left,16);expect(&other,right,16);REQUIRE(!owner->v.b&&pool_windows.bytes==2*unit);
    mtl_sub_begin();VPool *wide=read_view(cross,64);VBuffer before=capture_view(wide,cross,64);
    queued_update(memory+40*1024*1024,cross+8,payload);
    VBuffer updated_left=capture_view(l,cross+8,8),updated_right=capture_view(r,cross+16,8),after=capture_view(wide,cross,64);
    VPool *new_wide=read_view(base,2*unit+64);VBuffer new_after=capture_view(new_wide,cross,64);
    mtl_sub_commit();memcpy(updated,initial,64);memcpy(updated+8,payload,16);
    expect(&before,initial,64);expect(&updated_left,payload,8);expect(&updated_right,payload+8,8);
    expect(&after,updated,64);expect(&new_after,updated,64);REQUIRE(!memcmp((void *)(uintptr_t)cross,updated,64));
    REQUIRE(!owner->v.b&&pool_windows.created>=4);
    mtl_sub_begin();queued_update(memory+40*1024*1024,source,second);
    VPool *larger=read_view(base,source-base+unit);VBuffer replayed=capture_view(larger,source,16);
    mtl_sub_copy(source,destination,16);VBuffer src=capture_bytes(source,16),dst=capture_bytes(destination,16);mtl_sub_commit();
    expect(&replayed,second,16);expect(&src,second,16);expect(&dst,second,16);
    REQUIRE(!memcmp((void *)(uintptr_t)source,second,16)&&!memcmp((void *)(uintptr_t)destination,second,16));
    REQUIRE(owner->v.b&&pool_windows.promoted==1&&!pool_windows.bytes);
    G.present_count+=POOL_WINDOW_IDLE+1;mtl_sub_begin();mtl_sub_commit();
    REQUIRE(!owner->v.b&&pool_windows.demoted==1&&!pool_windows.bytes);
    mtl_sub_begin();src=capture_bytes(source,16);dst=capture_bytes(destination,16);mtl_sub_commit();
    expect(&src,second,16);expect(&dst,second,16);REQUIRE(!owner->v.b&&pool_windows.bytes==2*unit);
    mtl_pool_del(memory);REQUIRE(!pool_windows.head&&!pool_windows.bytes);
    dm_guest_write_prepare(memory+4096,16);memset(memory+4096,0x6b,16);mtl_pool_add_flags(memory,logical,0);
    owner=pool_find(base,logical,NULL);REQUIRE(owner&&owner->serial!=serial);
    mtl_sub_begin();first=capture_bytes(base+4096,16);mtl_sub_commit();memset(left,0x6b,16);expect(&first,left,16);
    mtl_pool_del(memory);REQUIRE(!pool_windows.head&&!pool_windows.bytes&&!munmap(memory,logical));
    DmVkMemoryStats stats;REQUIRE(!dm_vk_memory_get_stats(&stats)&&!stats.bytes[DM_VK_MEMORY_POOL]);
    printf("ANDROID_POOL_WINDOW_OK checks=%u actual_gpu_bytes=1 overlap_updates=1 promotion=1 fenced_reclaim=1 address_reuse=1 granule=%llu original_game=0\n",checks,(unsigned long long)unit);return 0;
}
