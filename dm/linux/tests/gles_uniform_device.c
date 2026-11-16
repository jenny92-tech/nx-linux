// INPUT: Production GLES deferred updates and a real driver; no game assets.
// OUTPUT: GPU bytes for immutable versions, overlaps, observers and writeback.
// POS: Regression for semantics-preserving uniform update coalescing.
#include "../../host/hle.h"
#undef die
#include "../../gfx/nvn/nvn_gl.c"
static SDL_Window *probe_window;
void crash_thread_init(void);
void *dm_android_native_window(void){return probe_window;}
void dm_android_front_presented(uint64_t frame){(void)frame;}
double front_display_refresh(char *name,size_t size){snprintf(name,size,"GLES uniform fixture");return 60;}
void dm_host_shutdown(const char *why){fprintf(stderr,"Unexpected shutdown: %s\n",why);_exit(2);}
void dm_shot_written(const char *path,int ok){(void)path;(void)ok;}
uint64_t soul_present_count(void){return 0;}
void dm_ui_text_entered(const char *text){(void)text;}
int front_text_input(const char *title,const char *sub,const char *guide,const char *initial,int max_chars,char *out,int out_cap){
    (void)title;(void)sub;(void)guide;(void)initial;(void)max_chars;(void)out;(void)out_cap;abort();
}
static unsigned checks;
#define REQUIRE(x) do{checks++;if(!(x)){fprintf(stderr,"GLES_UNIFORM_FAIL line=%d expression=%s\n",__LINE__,#x);abort();}}while(0)
static void gpu_copy(GLuint source,uint64_t from,GlRange destination){
    uint64_t offset=0;GlPool *p=gl_pool(destination.addr,destination.size,&offset);REQUIRE(p);gl_backing(p,offset+destination.size);
    glBindBuffer(GL_COPY_READ_BUFFER,source);glBindBuffer(GL_COPY_WRITE_BUFFER,p->backing.name);glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    glCopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,from,offset,destination.size);gl_mark(destination);
}
static void bytes_equal(GLuint buffer,uint64_t offset,const void *bytes,size_t size){
    glBindBuffer(GL_COPY_READ_BUFFER,buffer);const void *p=glMapBufferRange(GL_COPY_READ_BUFFER,offset,size,GL_MAP_READ_BIT);
    REQUIRE(p&&!memcmp(p,bytes,size));REQUIRE(glUnmapBuffer(GL_COPY_READ_BUFFER));
}
static void barrier(uint8_t *memory,uint32_t bits){
    op_hdr h={.op=OP_BARRIER,.size=16},end={.op=OP_END,.size=8};memcpy(memory,&h,8);memcpy(memory+8,&bits,4);memcpy(memory+16,&end,8);mtl_exec(memory);
}
static GLuint fixture_program(const char *fragment){
    const char *vertex="#version 320 es\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}";
    GLuint v=glCreateShader(GL_VERTEX_SHADER),f=glCreateShader(GL_FRAGMENT_SHADER),program=glCreateProgram();
    glShaderSource(v,1,&vertex,NULL);glCompileShader(v);glShaderSource(f,1,&fragment,NULL);glCompileShader(f);
    GLint ok;glGetShaderiv(v,GL_COMPILE_STATUS,&ok);REQUIRE(ok);glGetShaderiv(f,GL_COMPILE_STATUS,&ok);REQUIRE(ok);
    glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);glGetProgramiv(program,GL_LINK_STATUS,&ok);REQUIRE(ok);
    glDeleteShader(v);glDeleteShader(f);return program;
}
static void sampler_border_pixels(void){
    gl_enter();GLuint textures[2];glGenTextures(2,textures);glActiveTexture(GL_TEXTURE0);
    uint8_t red[16]={255,0,0,255,255,0,0,255,255,0,0,255,255,0,0,255};
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glBindTexture(GL_TEXTURE_2D,textures[0]);glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,2,2);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,red);
    glBindTexture(GL_TEXTURE_2D,textures[1]);glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,4,4);
    glBindFramebuffer(GL_FRAMEBUFFER,L.fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[1],0);REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    GLuint program=fixture_program("#version 320 es\nprecision highp float;uniform highp sampler2D src;uniform vec2 uv;layout(location=0) out vec4 c;void main(){c=texture(src,uv);}");
    glUseProgram(program);glUniform1i(glGetUniformLocation(program,"src"),0);GLint uv=glGetUniformLocation(program,"uv");REQUIRE(uv>=0);
    glViewport(0,0,4,4);glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glBindVertexArray(L.vao);
    samp_par p={.ws=NVN_WRAP_MODE_CLAMP_TO_BORDER,.wt=NVN_WRAP_MODE_CLAMP_TO_EDGE,.wr=NVN_WRAP_MODE_CLAMP_TO_EDGE,.cmp_func=1,.lod_max=10,.border={0,0,1,1}};
    gl_leave();mtl_reg_samp(0,&p);gl_enter();REQUIRE(L.sampler_border[0]);REQUIRE(gl_sampler_border_supported(0,GL_RGBA));REQUIRE(!gl_sampler_border_supported(0,GL_RED_INTEGER));
    glBindSampler(0,L.samplers[0]);glBindTexture(GL_TEXTURE_2D,textures[0]);
    const float points[4][2]={{-.25f,.5f},{.5f,.5f},{1.25f,.5f},{.5f,-.25f}};
    for(unsigned i=0;i<4;i++){
        glUniform2fv(uv,1,points[i]);glDrawArrays(GL_TRIANGLES,0,3);uint8_t pixel[4];glReadPixels(1,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        REQUIRE(!memcmp(pixel,(i==0||i==2)?(uint8_t[]){0,0,255,255}:(uint8_t[]){255,0,0,255},4));
    }
    /* Linear sampling on the exact edge blends border and texel equally. */
    p.minf=p.magf=1;gl_leave();mtl_reg_samp(0,&p);gl_enter();glBindSampler(0,L.samplers[0]);glUniform2f(uv,0,.5f);glDrawArrays(GL_TRIANGLES,0,3);
    uint8_t pixel[4];glReadPixels(1,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);REQUIRE(pixel[0]>=127&&pixel[0]<=128&&pixel[1]==0&&pixel[2]>=127&&pixel[2]<=128&&pixel[3]==255);
    p.border[1]=1;p.border[2]=0;gl_leave();mtl_reg_samp(0,&p);gl_enter();glBindSampler(0,L.samplers[0]);glUniform2f(uv,-.25f,.5f);glDrawArrays(GL_TRIANGLES,0,3);glReadPixels(1,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);REQUIRE(!memcmp(pixel,(uint8_t[]){0,255,0,255},4));
    p.ws=NVN_WRAP_MODE_CLAMP_TO_EDGE;gl_leave();mtl_reg_samp(0,&p);gl_enter();REQUIRE(!L.sampler_border[0]);REQUIRE(gl_sampler_border_supported(0,GL_RED_INTEGER));glBindSampler(0,L.samplers[0]);glDrawArrays(GL_TRIANGLES,0,3);glReadPixels(1,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);REQUIRE(!memcmp(pixel,(uint8_t[]){255,0,0,255},4));
    glBindSampler(0,0);glUseProgram(0);glDeleteProgram(program);glDeleteTextures(2,textures);gl_check("border sampler pixels");gl_leave();
}
static void coherent_pixels(GlRange r,GlRange out,uint8_t *memory){
    mtl_sub_begin();GLuint textures[2];glGenTextures(2,textures);glActiveTexture(GL_TEXTURE0);glBindSampler(0,0);
    for(unsigned i=0;i<2;i++){glBindTexture(GL_TEXTURE_2D,textures[i]);glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,4,4);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);}
    GLuint paint=fixture_program("#version 320 es\nprecision highp float;layout(location=0) out vec4 c;void main(){c=vec4(1,0,0,1);}");
    GLuint sample=fixture_program("#version 320 es\nprecision highp float;uniform highp sampler2D src;layout(location=0) out vec4 c;void main(){c=texture(src,vec2(0.5)).bgra;}");
    GLuint store=fixture_program("#version 320 es\nprecision highp float;precision highp int;layout(std430,binding=0) buffer B{uint v[];};layout(location=0) out vec4 c;void main(){if(gl_FragCoord.x<1.0&&gl_FragCoord.y<1.0)v[0]=0x76543210u;c=vec4(0,1,0,1);}");
    glBindFramebuffer(GL_FRAMEBUFFER,L.fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[0],0);REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glViewport(0,0,4,4);glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glBindVertexArray(L.vao);glUseProgram(paint);L.readonly_draw=1;
    uint64_t omitted=L.coherent_draw_barriers;glDrawArrays(GL_TRIANGLES,0,3);gl_draw_memory_barrier();REQUIRE(L.coherent_draw_barriers==omitted+1);
    /* Ordinary framebuffer output followed by sampling a different FBO is
     * coherent. No feedback loop and no imageStore/SSBO producer is involved. */
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[1],0);glBindTexture(GL_TEXTURE_2D,textures[0]);glUseProgram(sample);glUniform1i(glGetUniformLocation(sample,"src"),0);
    glDrawArrays(GL_TRIANGLES,0,3);gl_draw_memory_barrier();
    /* Read T0 into T1, then write T0 through an incoherent compute image. No
     * CPU readback/fence may hide the read→write hazard between these jobs. */
    const char *cs="#version 320 es\nlayout(local_size_x=4,local_size_y=4) in;layout(binding=0,rgba8) writeonly uniform highp image2D dst;void main(){imageStore(dst,ivec2(gl_LocalInvocationID.xy),vec4(0,1,0,1));}";
    GLuint shader=glCreateShader(GL_COMPUTE_SHADER),compute=glCreateProgram();glShaderSource(shader,1,&cs,NULL);glCompileShader(shader);GLint ok;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);REQUIRE(ok);glAttachShader(compute,shader);glLinkProgram(compute);glGetProgramiv(compute,GL_LINK_STATUS,&ok);REQUIRE(ok);
    glBindImageTexture(0,textures[0],0,GL_FALSE,0,GL_WRITE_ONLY,GL_RGBA8);glUseProgram(compute);L.readonly_draw=0;uint64_t writers=L.coherent_writer_barriers;
    gl_shader_write_barrier();REQUIRE(L.coherent_writer_barriers==writers+1);glDispatchCompute(1,1,1);glMemoryBarrier(GL_ALL_BARRIER_BITS);
    uint8_t pixels[64];glReadPixels(0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    for(unsigned i=0;i<16;i++)REQUIRE(!memcmp(pixels+i*4,(uint8_t[]){0,0,255,255},4));
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[0],0);glReadPixels(0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    for(unsigned i=0;i<16;i++)REQUIRE(!memcmp(pixels+i*4,(uint8_t[]){0,255,0,255},4));
    uint64_t offset;GlPool *p=gl_upload(r,&offset);glBindBufferRange(GL_SHADER_STORAGE_BUFFER,0,p->backing.name,offset,r.size);glUseProgram(store);L.readonly_draw=0;omitted=L.coherent_draw_barriers;
    gl_shader_write_barrier();glDrawArrays(GL_TRIANGLES,0,3);gl_draw_memory_barrier();REQUIRE(L.coherent_draw_barriers==omitted);gl_mark(r);
    /* The shader writer retains its actual visibility barrier before the
     * later API buffer copy; the filtered API barrier cannot stand in for it. */
    L.readonly_draw=1;GLuint snap=gl_read_buffer(r,&offset);gpu_copy(snap,offset,out);mtl_sub_commit();uint32_t value;memcpy(&value,memory+4096,4);REQUIRE(value==0x76543210u);
    gl_enter();glUseProgram(0);glDeleteProgram(paint);glDeleteProgram(sample);glDeleteProgram(store);glDeleteProgram(compute);glDeleteShader(shader);glDeleteTextures(2,textures);gl_check("coherent fixture");gl_leave();
}
int main(void){
    setenv("DM_UNIFORM_SNAPSHOT","1",1);setenv("DM_GLES_COHERENT_READS","1",1);setenv("DM_GLES_FILTER_UNSUPPORTED","0",1);
    crash_thread_init();REQUIRE(!SDL_Init(SDL_INIT_VIDEO));REQUIRE(!dm_linux_gles_configure());
    probe_window=SDL_CreateWindow("GLES uniform versions",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_SHOWN);
    REQUIRE(probe_window&&dm_linux_gles_bind_window(probe_window));dm_vk_set_window(probe_window);mtl_init();REQUIRE(L.uniform_snapshot);
    uint8_t *memory=mmap(NULL,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);REQUIRE(memory!=MAP_FAILED);nvn_dirty_add_range(memory,65536);mtl_pool_add_flags(memory,65536,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    GlRange r={(uintptr_t)memory,64},out={(uintptr_t)(memory+4096),64};uint32_t original[16],a[16],b[6],c[16],mixed[16],gpu[16];
    for(unsigned i=0;i<16;i++){original[i]=100+i;a[i]=0x1000+i;c[i]=0x3000+i;gpu[i]=0x4000+i;}for(unsigned i=0;i<6;i++)b[i]=0x2000+i;
    memcpy(memory,original,64);memcpy(mixed,a,64);memcpy(mixed+3,b,sizeof b);
    mtl_sub_begin();L.readonly_draw=1;uint64_t offset;
    REQUIRE(gl_uniform_defer(r,(uint8_t *)a));GLuint first=gl_read_buffer(r,&offset);REQUIRE(!offset);
    REQUIRE(gl_read_buffer(r,&offset)==first);REQUIRE(L.uniform_cache_hits==1);
    REQUIRE(gl_uniform_defer((GlRange){r.addr+12,sizeof b},(uint8_t *)b));GLuint second=gl_read_buffer(r,&offset);
    REQUIRE(gl_uniform_defer(r,(uint8_t *)c));GLuint third=gl_read_buffer(r,&offset);REQUIRE(first!=second&&second!=third);
    REQUIRE(!memcmp(memory,original,64));uint64_t updates=L.uniform_updates;
    GlPool *p=gl_upload(r,&offset);REQUIRE(!L.nuniform_pending&&L.uniform_updates==updates+1);bytes_equal(p->backing.name,offset,c,64);
    /* Queue consumers of all three versions, then prove the old versions
     * survived the final canonical overwrite and the real completion fence. */
    gpu_copy(first,0,out);gpu_copy(second,0,(GlRange){out.addr+64,64});gpu_copy(third,0,(GlRange){out.addr+128,64});
    REQUIRE(!memcmp(memory,original,64));mtl_sub_commit();REQUIRE(!memcmp(memory,c,64));
    REQUIRE(!memcmp(memory+4096,a,64));REQUIRE(!memcmp(memory+4160,mixed,64));REQUIRE(!memcmp(memory+4224,c,64));
    mtl_sub_begin();L.readonly_draw=1;p=gl_upload(r,&offset);GLuint producer=gl_temp(64,gpu);gpu_copy(producer,0,r);
    REQUIRE(gl_uniform_defer((GlRange){r.addr+12,sizeof b},(uint8_t *)b));GLuint after_gpu=gl_read_buffer(r,&offset);
    memcpy(mixed,gpu,64);memcpy(mixed+3,b,sizeof b);REQUIRE(!L.nuniform_pending);bytes_equal(after_gpu,offset,mixed,64);
    gpu_copy(after_gpu,offset,out);mtl_sub_commit();REQUIRE(!memcmp(memory,mixed,64));REQUIRE(!memcmp(memory+4096,mixed,64));
    /* Public non-uniform copies must flush a queued destination as well as
     * the source. The newer GPU copy wins over the older delayed CPU update. */
    mtl_sub_begin();L.readonly_draw=1;REQUIRE(gl_uniform_defer(out,(uint8_t *)a));mtl_sub_copy(r.addr,out.addr,64);mtl_sub_commit();REQUIRE(!memcmp(memory+4096,mixed,64));
    mtl_sub_begin();L.readonly_draw=1;REQUIRE(gl_uniform_defer(r,(uint8_t *)a));
    barrier(memory+8192,NVN_BARRIER_INVALIDATE_SHADER_BIT);REQUIRE(L.nuniform_pending==1);
    barrier(memory+8192,NVN_BARRIER_INVALIDATE_SHADER_BIT|NVN_BARRIER_ORDER_FRAGMENTS_BIT);REQUIRE(!L.nuniform_pending);
    REQUIRE(gl_uniform_defer(r,(uint8_t *)c));L.readonly_draw=0;GLuint canonical=gl_read_buffer(r,&offset);REQUIRE(!L.nuniform_pending);bytes_equal(canonical,offset,c,64);mtl_sub_commit();REQUIRE(!memcmp(memory,c,64));
    GlShader facts={0};facts.h.nb=1;facts.b[0]=(GlBinding){.kind=3,.type=7,.flags=1};REQUIRE(!gl_shader_readonly(&facts));facts.b[0].flags=0;REQUIRE(gl_shader_readonly(&facts));
    mtl_sub_begin();L.readonly_draw=1;L.pools[0].flags|=NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS;REQUIRE(!gl_uniform_defer(r,(uint8_t *)a));L.pools[0].flags&=~NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS;
    /* Resource texture reuse is another non-uniform observer, even when it
     * bypasses gl_upload and copies already-owned GPU bytes directly. */
    REQUIRE(gl_uniform_defer(r,(uint8_t *)a));L.resource_reuse=1;GLuint texture=gl_resource_snapshot(r,GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,4,0);REQUIRE(texture);REQUIRE(!L.nuniform_pending);
    glBindFramebuffer(GL_FRAMEBUFFER,L.fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    uint32_t texels[16][4];glReadPixels(0,0,16,1,GL_RGBA_INTEGER,GL_UNSIGNED_INT,texels);for(unsigned i=0;i<16;i++)REQUIRE(texels[i][0]==a[i]);
    mtl_sub_commit();REQUIRE(!memcmp(memory,a,64));
    coherent_pixels(r,out,memory);
    sampler_border_pixels();
    gl_enter();REQUIRE(glGetError()==GL_NO_ERROR);gl_leave();mtl_pool_del(memory);nvn_dirty_remove_range(memory);REQUIRE(!munmap(memory,65536));
    printf("{\"passed\":true,\"checks\":%u,\"immutable_gpu_versions\":true,\"partial_overlap\":true,\"gpu_writer_fallback\":true,\"copy_observer\":true,\"barrier_fallback\":true,\"final_cpu_writeback\":true,\"resource_observer\":true,\"framebuffer_sampling\":true,\"fragment_storage_write_visibility\":true,\"game_executed\":false}\n",checks);
    SDL_DestroyWindow(probe_window);SDL_Quit();return 0;
}
