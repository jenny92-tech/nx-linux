/* Actual descriptor-selected GPU bytes, ordered CPU updates and writeback.
 * Independent expected byte arrays; no original game or production identity. */
#include "../../host/hle.h"
#undef die
#include "../../gfx/nvn/nvn_vk.c"
#include "../memory_guard.h"
static SDL_Window *probe_window;
void crash_thread_init(void);
void *dm_android_native_window(void){return probe_window;}
void dm_android_front_presented(uint64_t frame){(void)frame;}
double front_display_refresh(char *name,size_t size){snprintf(name,size,"Uniform binding fixture");return 60;}
void dm_host_shutdown(const char *why){fprintf(stderr,"Unexpected shutdown: %s\n",why);_exit(2);}
void dm_shot_written(const char *path,int ok){(void)path;(void)ok;}
static unsigned checks;
static VkBuffer last_bound_buffer;
static uint8_t *barrier_storage;
#define REQUIRE(x) do{checks++;if(!(x)){fprintf(stderr,"UNIFORM_BINDING_FAIL line=%d expression=%s\n",__LINE__,#x);abort();}}while(0)
static void update(uint8_t *commands,uint64_t address,const void *payload,uint32_t bytes){
    REQUIRE(bytes<=64&&bytes%4==0);
    op_hdr head={OP_UPDATE_UB,(uint32_t)(24+bytes)},end={OP_END,8};uint64_t size=bytes;
    memcpy(commands,&head,8);memcpy(commands+8,&address,8);memcpy(commands+16,&size,8);memcpy(commands+24,payload,bytes);memcpy(commands+24+bytes,&end,8);mtl_exec(commands);
}
static VBuffer capture_buffer(VkBuffer source,uint64_t offset,uint64_t bytes){
    VBuffer out=buffer_create_kind(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT,1,DM_VK_MEMORY_READBACK);
    all_barrier();VkBufferCopy copy={offset,0,bytes};vkCmdCopyBuffer(S.command,source,out.b,1,&copy);return out;
}
static VkDescriptorBufferInfo selected_binding(VShader *shader,uint64_t address,uint64_t bytes){
    G.state.uniform[shader->h.stage][0]=(Range){address,bytes};
    REQUIRE(S.desc_entries&&S.desc_arena);fprintf(stderr,"FIXTURE_BIND before bind\n");VkDescriptorSet set=bind_shader(shader);fprintf(stderr,"FIXTURE_BIND after bind\n");REQUIRE(set!=VK_NULL_HANDLE);
    for(unsigned i=0;i<S.ndesc;i++)if(S.desc_entries[i].set==set){
        const DescEntry *e=&S.desc_entries[i];REQUIRE(e->nb==1);
        VkDescriptorBufferInfo info;memcpy(&info,S.desc_arena+e->off,sizeof info);
        REQUIRE(info.buffer!=VK_NULL_HANDLE&&info.range==bytes);last_bound_buffer=info.buffer;return info;
    }
    REQUIRE(0);return (VkDescriptorBufferInfo){0};
}
static VBuffer capture_binding(VShader *shader,uint64_t address,uint64_t bytes){VkDescriptorBufferInfo info=selected_binding(shader,address,bytes);return capture_buffer(info.buffer,info.offset,bytes);}
static void shader_barrier(uint32_t bits){uint8_t *command=barrier_storage;memset(command,0,24);op_hdr h={OP_BARRIER,16},end={OP_END,8};memcpy(command,&h,8);memcpy(command+8,&bits,4);memcpy(command+16,&end,8);mtl_exec(command);}
static void empty_pass(VkRenderPass pass,VkFramebuffer framebuffer){
    VkRenderPassBeginInfo begin={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,.framebuffer=framebuffer,.renderArea={{0,0},{64,64}}};
    vkCmdBeginRenderPass(S.command,&begin,VK_SUBPASS_CONTENTS_INLINE);S.in_pass=1;S.read_snapshots_allowed=1;
}
static VBuffer capture_pool(uint64_t address,uint64_t bytes){uint64_t offset;VPool *p=pool_read(address,bytes,&offset);return capture_buffer(p->v.b,offset,bytes);}
static unsigned expectation;
static void expect(VBuffer *value,const void *wanted,size_t bytes){
    expectation++;
    if(memcmp(value->map,wanted,bytes)){
        fprintf(stderr,"EXPECTATION_FAIL index=%u actual=",expectation);for(size_t i=0;i<bytes;i++)fprintf(stderr,"%02x",value->map[i]);
        fprintf(stderr," expected=");for(size_t i=0;i<bytes;i++)fprintf(stderr,"%02x",((const uint8_t *)wanted)[i]);fprintf(stderr,"\n");
    }
    REQUIRE(!memcmp(value->map,wanted,bytes));buffer_free(value);
}
int main(int argc,char **argv){
    REQUIRE(argc==2);unsetenv("DM_LINUX_SHARED_POOLS");unsetenv("DM_LINUX_SHARED_ASYNC");unsetenv("DM_LINUX_POOL_WINDOWS");
    setenv("DM_DISABLE_ASYNC_COMMIT","1",1);unsetenv("DM_DISABLE_DESC_CACHE");
    dm_linux_memory_guard_init();crash_thread_init();REQUIRE(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS));
    probe_window=SDL_CreateWindow("Uniform snapshot byte verification",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,64,64,SDL_WINDOW_SHOWN|(dm_linux_vulkan_private_requested()?0:SDL_WINDOW_VULKAN));
    REQUIRE(probe_window&&dm_linux_vulkan_bind_window(probe_window));dm_vk_set_window(probe_window);mtl_init();G.render_slots=1;
    uint8_t *memory=mmap(NULL,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);REQUIRE(memory!=MAP_FAILED);mtl_pool_add_flags(memory,65536,0);barrier_storage=memory+16384;
    uint64_t address=(uint64_t)(uintptr_t)memory+1024,other=(uint64_t)(uintptr_t)memory+4096,source=(uint64_t)(uintptr_t)memory+6144;
    uint8_t original[32],first[32],second[32],last[32],gpu[32],p0[16],p1[16];
    for(unsigned i=0;i<32;i++){original[i]=(uint8_t)(i+1);last[i]=(uint8_t)(0xc0+i);gpu[i]=(uint8_t)(0x60+i);}
    for(unsigned i=0;i<16;i++){p0[i]=(uint8_t)(0x30+i);p1[i]=(uint8_t)(0x90+i);}
    memcpy(first,original,32);memcpy(first,p0,16);memcpy(second,first,32);memcpy(second+8,p1,16);memcpy(memory+1024,original,32);memcpy(memory+6144,gpu,32);
    VShader shader={0};shader.h.stage=0;shader.h.nb=1;shader.b[0]=(RBinding){.set=0,.binding=0,.type=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.kind=2,.slot=0,.count=1};
    VkDescriptorSetLayoutBinding binding={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_VERTEX_BIT};
    VkDescriptorSetLayoutCreateInfo layout={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};CHECK(vkCreateDescriptorSetLayout(G.device,&layout,NULL,&shader.layout));
    mtl_sub_begin();S.read_snapshots_allowed=1;VBuffer a=capture_binding(&shader,address,32);
    update(memory+8192,address,p0,16);VBuffer b=capture_binding(&shader,address,32);
    update(memory+8320,address+8,p1,16);VBuffer c=capture_binding(&shader,address,32);
    update(memory+8448,address,last,32);VBuffer d=capture_binding(&shader,address,32);
    mtl_sub_copy(address,other,32);VBuffer e=capture_pool(other,32);
    REQUIRE(!memcmp(memory+1024,original,32));mtl_sub_commit();
    expect(&a,original,32);expect(&b,first,32);expect(&c,second,32);expect(&d,last,32);expect(&e,last,32);REQUIRE(!memcmp(memory+1024,last,32)&&!memcmp(memory+4096,last,32));
    /* A later GPU producer must win over an older deferred CPU update. */
    mtl_sub_begin();S.read_snapshots_allowed=1;update(memory+8192,address,p0,16);mtl_sub_copy(source,address,32);a=capture_binding(&shader,address,32);mtl_sub_commit();expect(&a,gpu,32);REQUIRE(!memcmp(memory+1024,gpu,32));
    /* Final writeback is required even when no uniform consumer follows. */
    mtl_sub_begin();S.read_snapshots_allowed=1;update(memory+8192,address,last,32);REQUIRE(!memcmp(memory+1024,gpu,32));mtl_sub_commit();REQUIRE(!memcmp(memory+1024,last,32));
    if(G.uniform_snapshot){
        /* Real Vulkan pass lifetime plus immutable descriptor-selected GPU bytes.
         * No synthetic raster/shader pixel claim: gameplay covers real draws. */
        G.uniform_shader_barrier=1;
        VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS};
        VkRenderPassCreateInfo ri={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.subpassCount=1,.pSubpasses=&sub};VkRenderPass pass;CHECK(vkCreateRenderPass(G.device,&ri,NULL,&pass));
        VkFramebufferCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,.width=64,.height=64,.layers=1};VkFramebuffer framebuffer;CHECK(vkCreateFramebuffer(G.device,&fi,NULL,&framebuffer));
        mtl_sub_begin();S.read_snapshots_allowed=1;update(memory+8192,address,p0,16);VkDescriptorBufferInfo old=selected_binding(&shader,address,32);
        empty_pass(pass,framebuffer);shader_barrier(NVN_BARRIER_INVALIDATE_SHADER_BIT);REQUIRE(S.in_pass&&S.nuniform_pending==1);
        update(memory+8320,address+8,p1,16);VkDescriptorBufferInfo next=selected_binding(&shader,address,32);REQUIRE(S.in_pass&&old.buffer!=next.buffer);
        shader_barrier(NVN_BARRIER_INVALIDATE_SHADER_BIT);REQUIRE(S.in_pass&&S.nuniform_pending==2);pass_end();
        a=capture_buffer(old.buffer,old.offset,32);b=capture_buffer(next.buffer,next.offset,32);mtl_sub_commit();
        uint8_t want0[32],want1[32];memcpy(want0,last,32);memcpy(want0,p0,16);memcpy(want1,want0,32);memcpy(want1+8,p1,16);expect(&a,want0,32);expect(&b,want1,32);REQUIRE(!memcmp(memory+1024,want1,32));
        /* Mixed/unknown bits retain the original barrier and canonical bytes. */
        uint32_t rejected[]={0,NVN_BARRIER_ORDER_PRIMITIVES_BIT,NVN_BARRIER_INVALIDATE_TEXTURE_BIT,0x21,0x60};
        for(unsigned i=0;i<sizeof rejected/sizeof *rejected;i++){
            mtl_sub_begin();S.read_snapshots_allowed=1;update(memory+8192,address,last,32);empty_pass(pass,framebuffer);shader_barrier(rejected[i]);REQUIRE(!S.in_pass&&!S.nuniform_pending);mtl_sub_commit();REQUIRE(!memcmp(memory+1024,last,32));
        }
        /* Even a non-overlapping earlier GPU pool writer blocks the shortcut. */
        mtl_sub_begin();S.read_snapshots_allowed=1;mtl_sub_copy(source,other,32);update(memory+8192,address,last,32);empty_pass(pass,framebuffer);shader_barrier(0x20);REQUIRE(!S.in_pass&&!S.nuniform_pending);mtl_sub_commit();REQUIRE(!memcmp(memory+1024,last,32));
        /* A real wide dependency covers transfer writes for every future
         * graphics binding. Retain the ledger for byte provenance/writeback. */
        mtl_sub_begin();S.read_snapshots_allowed=1;mtl_sub_copy(source,other,32);
        unsigned gpu_prefix=(unsigned)S.nwrites;REQUIRE(!S.uniform_gpu_visible_writes);
        hz_pass_sync();REQUIRE(S.uniform_gpu_visible_writes==gpu_prefix);
        update(memory+8192,address,last,32);empty_pass(pass,framebuffer);shader_barrier(0x20);
        REQUIRE(S.in_pass&&S.nuniform_pending==1&&S.nwrites>(int)gpu_prefix);
        pass_end();a=capture_binding(&shader,address,32);b=capture_pool(other,32);mtl_sub_commit();
        expect(&a,last,32);expect(&b,gpu,32);REQUIRE(!memcmp(memory+4096,gpu,32));
        /* A new transfer after the covered prefix must still reject elision. */
        mtl_sub_begin();S.read_snapshots_allowed=1;mtl_sub_copy(source,other,32);hz_pass_sync();
        gpu_prefix=S.uniform_gpu_visible_writes;mtl_sub_copy(source,other+64,32);
        REQUIRE(S.uniform_gpu_visible_writes==gpu_prefix&&(unsigned)S.nwrites>gpu_prefix);
        update(memory+8192,address,last,32);empty_pass(pass,framebuffer);shader_barrier(0x20);
        REQUIRE(!S.in_pass&&!S.nuniform_pending);mtl_sub_commit();REQUIRE(!memcmp(memory+4160,gpu,32));
        /* Visibility does not turn unknown GPU bytes into CPU provenance.
         * A later partial update must overlay the actual copied GPU value. */
        mtl_sub_begin();S.read_snapshots_allowed=1;mtl_sub_copy(source,address,32);hz_pass_sync();
        update(memory+8192,address,p0,16);empty_pass(pass,framebuffer);shader_barrier(0x20);
        REQUIRE(S.in_pass&&S.nuniform_pending==1);VkDescriptorBufferInfo overlap_binding=selected_binding(&shader,address,32);
        REQUIRE(!S.in_pass&&!S.nuniform_pending);a=capture_buffer(overlap_binding.buffer,overlap_binding.offset,32);mtl_sub_commit();
        uint8_t overlap_want[32];memcpy(overlap_want,gpu,32);memcpy(overlap_want,p0,16);expect(&a,overlap_want,32);REQUIRE(!memcmp(memory+1024,overlap_want,32));
        /* Attachment-only visibility and diagnostics cannot cover pool writes. */
        mtl_sub_begin();S.read_snapshots_allowed=1;mark_write(other,32);hz_add(1u<<HZ_GF,HZ_ATT_STAGES,HZ_ATT_ACCESS);hz_pass_sync();
        REQUIRE(!S.uniform_gpu_visible_writes);mtl_sub_commit();
        mtl_sub_begin();S.read_snapshots_allowed=1;mtl_sub_copy(source,other,32);
        int saved_diag=G.hz_diag;G.hz_diag|=8;hz_pass_sync();G.hz_diag=saved_diag;REQUIRE(!S.uniform_gpu_visible_writes);
        mtl_sub_commit();
        /* Image/SSBO shader history cannot be cleared by opening a new pass. */
        mtl_sub_begin();S.read_snapshots_allowed=1;S.uniform_shader_write_seen=1;update(memory+8192,address,gpu,32);empty_pass(pass,framebuffer);shader_barrier(0x20);REQUIRE(!S.in_pass&&!S.nuniform_pending);mtl_sub_commit();REQUIRE(!memcmp(memory+1024,gpu,32));
        /* A later ordinary consumer must still materialize the deferred value. */
        mtl_sub_begin();S.read_snapshots_allowed=1;update(memory+8192,address,last,32);(void)selected_binding(&shader,address,32);empty_pass(pass,framebuffer);shader_barrier(0x20);REQUIRE(S.in_pass);mtl_sub_copy(address,other,32);REQUIRE(!S.in_pass&&!S.nuniform_pending);a=capture_pool(other,32);mtl_sub_commit();expect(&a,last,32);
        vkDestroyFramebuffer(G.device,framebuffer,NULL);vkDestroyRenderPass(G.device,pass,NULL);
        G.uniform_shader_barrier=0; /* Keep the independent fence regression on its original path. */
    }
    /* Finite real GPU transfers keep the render-slot fence unsignaled without
     * introducing Vulkan entry points absent from the selected WSI dispatch.
     * Assert NOT_READY before requesting reuse of that same real slot. */
    VBuffer busy0=buffer_create_kind(4*1024*1024,VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,1,DM_VK_MEMORY_TEMP);
    VBuffer busy1=buffer_create_kind(4*1024*1024,VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,1,DM_VK_MEMORY_TEMP);
    G.async_enabled=1;
    mtl_sub_begin();S.read_snapshots_allowed=1;update(memory+8192,address,gpu,32);a=capture_binding(&shader,address,32);VkBuffer in_flight=last_bound_buffer;
    VkBufferCopy traffic={0,0,4*1024*1024};
    for(unsigned i=0;i<32;i++){all_barrier();vkCmdCopyBuffer(S.command,busy0.b,busy1.b,1,&traffic);all_barrier();vkCmdCopyBuffer(S.command,busy1.b,busy0.b,1,&traffic);}
    fprintf(stderr,"FIXTURE_PHASE pre_async_submit\n");mtl_sub_commit_async();fprintf(stderr,"FIXTURE_PHASE post_async_submit\n");
    REQUIRE(vkGetFenceStatus(G.device,G.submission_slot.gpu_fence)==VK_NOT_READY);
    if(G.uniform_snapshot){int held=0;for(int i=0;i<G.submission_slot.ntemporary;i++)held|=G.submission_slot.temporaries[i]->b==in_flight;REQUIRE(held);}
    uint64_t ticket=G.fence,old_wait=G.profile_slot_wait_ns;
    /* The supported single render slot must await its own real fence and
     * publication before its descriptor/temporary registry can be reset. */
    mtl_sub_begin();S.read_snapshots_allowed=1;REQUIRE(atomic_load(&G.completed)>=ticket&&G.profile_slot_wait_ns>old_wait);expect(&a,gpu,32);
    update(memory+8320,address,last,32);b=capture_binding(&shader,address,32);mtl_sub_commit();expect(&b,last,32);
    buffer_free(&busy0);buffer_free(&busy1);mtl_finish(); /* Shader layout has process lifetime, like backend shaders. */
    mtl_pool_del(memory);REQUIRE(!munmap(memory,65536));
    if(G.uniform_snapshot)REQUIRE(G.profile_uniform_deferred&&G.profile_uniform_snapshots);
    FILE *out=fopen(argv[1],"w");REQUIRE(out);fprintf(out,"{\"passed\":true,\"checks\":%u,\"enabled\":%d,\"deferred_updates\":%llu,\"snapshots\":%llu,\"gpu_descriptor_bytes\":true,\"partial_overlap\":true,\"consumer_copy\":true,\"gpu_writer_wins\":true,\"final_writeback\":true,\"actual_fence_ownership\":true,\"game_executed\":false}\n",checks,G.uniform_snapshot,(unsigned long long)G.profile_uniform_deferred,(unsigned long long)G.profile_uniform_snapshots);REQUIRE(!fclose(out));SDL_DestroyWindow(probe_window);SDL_Quit();return 0;
}
