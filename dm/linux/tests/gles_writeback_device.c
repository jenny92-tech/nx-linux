// INPUT: Production writeback module, real GPU writes and two live pool identities.
// OUTPUT: Exact overlapping publication, bounded fallback and completed reuse checks.
// POS: Native GLES functional regression; no guest/game runtime is required.
#include <EGL/egl.h>
#include "../../gfx/nvn/nvn_gl_buffer.h"
#include "../../gfx/nvn/nvn_vk_writeback_plan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GL_WRITES 4096
typedef struct {uint64_t addr,size;} GlRange;
typedef struct {uint8_t *guest;uint64_t size,serial;NvnGlBuffer backing;} GlPool;
static struct {
    GlRange writes[GL_WRITES];unsigned nwrites;
    NvnVkWritebackSpan writeback_plan[GL_WRITES];size_t writeback_count;
    GLuint download_buffer;uint64_t download_total,download_calls,download_bytes,download_maps,direct_readback_maps,writeback_bytes;int download_staged;
} L;
static GlPool pools[2];static unsigned checks,prepares;
static _Noreturn void gl_stop(const char *what,uint64_t n){fprintf(stderr,"STOP %s %llu\n",what,(unsigned long long)n);exit(2);}
static void check(int ok,const char *what){checks++;if(!ok){fprintf(stderr,"FAIL %s GL=%#x EGL=%#x\n",what,glGetError(),eglGetError());exit(2);}}
static void gl_check(const char *what){GLenum e=glGetError();if(e)gl_stop(what,e);}
static GlPool *gl_pool(uint64_t addr,uint64_t size,uint64_t *offset){
    for(unsigned i=0;i<2;i++){GlPool *p=&pools[i];uint64_t base=(uint64_t)(uintptr_t)p->guest;
        if(addr>=base&&addr-base<=p->size&&size<=p->size-(addr-base)){if(offset)*offset=addr-base;return p;}}
    return NULL;
}
static void dm_guest_write_prepare(void *p,size_t n){check(gl_pool((uint64_t)(uintptr_t)p,n,NULL)!=NULL,"publication destination live");prepares++;}
#include "../../gfx/nvn/nvn_gl_writeback.inc"
static void write_gpu(GLuint program,unsigned pool,uint32_t base){
    glUseProgram(program);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,pools[pool].backing.name);
    glUniform1ui(glGetUniformLocation(program,"base"),base);glDispatchCompute(1,1,1);
}
int main(void){
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);check(eglInitialize(display,NULL,NULL)&&eglBindAPI(EGL_OPENGL_ES_API),"EGL init");
    const EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_NONE};EGLConfig config;EGLint nc;
    check(eglChooseConfig(display,ca,&config,1,&nc)&&nc==1,"config");
    const EGLint aa[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,2,EGL_NONE},pa[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,aa);EGLSurface surface=eglCreatePbufferSurface(display,config,pa);
    check(context!=EGL_NO_CONTEXT&&surface!=EGL_NO_SURFACE&&eglMakeCurrent(display,surface,surface,context),"context");
    const size_t size=6*1024*1024;uint8_t *expected=malloc(size);check(expected!=NULL,"expected allocation");
    for(size_t i=0;i<size;i++)expected[i]=(uint8_t)(i*17+3);
    for(unsigned i=0;i<2;i++){
        GlPool *p=&pools[i];p->guest=malloc(size);check(p->guest!=NULL,"CPU allocation");memset(p->guest,0xcc,size);p->size=size;p->serial=i+1;
        check(nvn_gl_buffer_reserve(&p->backing,size,size)==GL_NO_ERROR,"GPU allocation");
        glBindBuffer(GL_COPY_WRITE_BUFFER,p->backing.name);glBufferSubData(GL_COPY_WRITE_BUFFER,0,size,expected);
    }
    const char *source="#version 320 es\nlayout(local_size_x=64) in;layout(std430,binding=0) buffer B{uint v[];};uniform uint base;void main(){uint i=gl_LocalInvocationID.x;v[i]=base+i;}";
    GLuint shader=glCreateShader(GL_COMPUTE_SHADER);glShaderSource(shader,1,&source,NULL);glCompileShader(shader);GLint ok;
    glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);check(ok,"compute compile");GLuint program=glCreateProgram();glAttachShader(program,shader);glLinkProgram(program);glGetProgramiv(program,GL_LINK_STATUS,&ok);check(ok,"compute link");
    uint32_t first[64],second[64];for(unsigned i=0;i<64;i++){first[i]=1000+i;second[i]=2000+i;}
    write_gpu(program,0,1000);write_gpu(program,1,2000);
    uint64_t a=(uint64_t)(uintptr_t)pools[0].guest,b=(uint64_t)(uintptr_t)pools[1].guest;
    /* Out-of-order duplicate/overlapping spans merge only within one pool. */
    L.writes[L.nwrites++]=(GlRange){a+64,192};L.writes[L.nwrites++]=(GlRange){b,256};L.writes[L.nwrites++]=(GlRange){a,128};L.writes[L.nwrites++]=(GlRange){a,4};
    gl_writeback_queue();check(L.download_staged&&L.writeback_count==2&&L.download_total==512,"bounded coalesced staging");
    check(pools[0].guest[0]==0xcc&&pools[1].guest[0]==0xcc&&prepares==0,"queue does not publish CPU bytes");
    /* A later GPU modification cannot change the already queued snapshot. */
    write_gpu(program,0,3000);glFinish();gl_writeback_publish();
    check(!memcmp(pools[0].guest,first,256)&&!memcmp(pools[1].guest,second,256),"ordered GPU snapshot publication");
    check(pools[0].guest[256]==0xcc&&pools[1].guest[256]==0xcc,"unwritten bytes untouched");
    check(prepares==2&&L.download_maps==1&&L.writeback_bytes==512,"one map and exact unique bytes");
    L.nwrites=1;L.writes[0]=(GlRange){a,256};gl_writeback_queue();glFinish();gl_writeback_publish();
    for(unsigned i=0;i<64;i++)first[i]=3000+i;
    check(!memcmp(pools[0].guest,first,256)&&L.download_maps==2,"same address observes next GPU generation");
    memcpy(expected,first,256);L.writes[0]=(GlRange){a,GL_DOWNLOAD_BUDGET+4};
    gl_writeback_queue();check(!L.download_staged&&L.download_total==GL_DOWNLOAD_BUDGET+4,"large transfer uses direct fallback");
    glFinish();gl_writeback_publish();check(!memcmp(pools[0].guest,expected,GL_DOWNLOAD_BUDGET+4)&&L.direct_readback_maps==1,"large fallback contents");
    L.nwrites=0;uint64_t maps=L.download_maps;gl_writeback_queue();gl_writeback_publish();check(!L.writeback_count&&!L.download_staged&&L.download_maps==maps,"empty commit");
    check(glGetError()==GL_NO_ERROR,"GL errors");
    printf("{\"renderer\":\"%s\",\"checks\":%u,\"overlap_coalesced\":true,\"gpu_copy_order\":true,\"completed_reuse\":true,\"bounded_fallback\":true,\"staging_maps\":%llu,\"direct_maps\":%llu}\n",glGetString(GL_RENDERER),checks,(unsigned long long)L.download_maps,(unsigned long long)L.direct_readback_maps);
    glDeleteBuffers(1,&L.download_buffer);for(unsigned i=0;i<2;i++){glDeleteBuffers(1,&pools[i].backing.name);free(pools[i].guest);}free(expected);glDeleteProgram(program);glDeleteShader(shader);
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);return 0;
}
