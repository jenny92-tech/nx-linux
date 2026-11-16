// INPUT: A real GLES 3.2 context and the production lazy-buffer module.
// OUTPUT: Byte-exact GPU-write preservation, binding identity and bounds checks.
// POS: Functional regression for pool backing growth; not a performance test.
#include <EGL/egl.h>
#include "../../gfx/nvn/nvn_gl_buffer.h"
#include "../../gfx/nvn/nvn_transfer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int ok,const char *what){
    if(!ok){fprintf(stderr,"STOP %s EGL=%#x GL=%#x\n",what,eglGetError(),glGetError());exit(2);}
}
static GLuint compute(const char *source){
    GLuint shader=glCreateShader(GL_COMPUTE_SHADER);glShaderSource(shader,1,&source,NULL);glCompileShader(shader);
    GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);check(ok,"compute compile");
    GLuint p=glCreateProgram();glAttachShader(p,shader);glLinkProgram(p);glGetProgramiv(p,GL_LINK_STATUS,&ok);check(ok,"compute link");glDeleteShader(shader);return p;
}
static void bytes_equal(GLuint buffer,uint64_t offset,const uint32_t *expected,size_t size){
    glBindBuffer(GL_COPY_READ_BUFFER,buffer);const void *data=glMapBufferRange(GL_COPY_READ_BUFFER,offset,size,GL_MAP_READ_BIT);
    if(data&&memcmp(data,expected,size)){const uint32_t *words=data;for(size_t i=0;i<size/4;i++)if(words[i]!=expected[i]){fprintf(stderr,"MISMATCH offset=%llu index=%zu got=%#x expected=%#x\n",(unsigned long long)offset,i,words[i],expected[i]);break;}}
    check(data&&!memcmp(data,expected,size),"GPU contents");check(glUnmapBuffer(GL_COPY_READ_BUFFER),"unmap");
}
int main(void){
    EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);check(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_ES_API),"EGL init");
    const EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_NONE};EGLConfig config;EGLint nc=0;
    check(eglChooseConfig(d,ca,&config,1,&nc)&&nc==1,"config");
    const EGLint ctxa[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,2,EGL_NONE},pa[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};
    EGLContext ctx=eglCreateContext(d,config,EGL_NO_CONTEXT,ctxa);EGLSurface surface=eglCreatePbufferSurface(d,config,pa);
    check(ctx!=EGL_NO_CONTEXT&&surface!=EGL_NO_SURFACE&&eglMakeCurrent(d,surface,surface,ctx),"context");
    const uint64_t mib=1024*1024,limit=8*mib+123;NvnGlBuffer b={0};
    check(nvn_gl_buffer_reserve(&b,1,limit)==GL_NO_ERROR&&b.capacity==mib,"initial lazy allocation");GLuint name=b.name;
    uint32_t original[64],modified[64],combined[64];for(unsigned i=0;i<64;i++){original[i]=i*17+3;modified[i]=original[i]^0xa5a5u;combined[i]=original[i]+modified[i];}
    glBindBuffer(GL_COPY_WRITE_BUFFER,name);glBufferSubData(GL_COPY_WRITE_BUFFER,0,sizeof original,original);glBufferSubData(GL_COPY_WRITE_BUFFER,256,sizeof original,original);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER,0,name,256,sizeof original);glBindBufferRange(GL_UNIFORM_BUFFER,0,name,0,sizeof original);
    GLuint vao;glGenVertexArrays(1,&vao);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,name);glVertexAttribIPointer(0,1,GL_UNSIGNED_INT,4,(void *)256);
    GLuint write=compute("#version 320 es\nlayout(local_size_x=64) in;layout(std430,binding=0) buffer B {uint v[];};void main(){v[gl_LocalInvocationID.x]^=0xa5a5u;}");
    glUseProgram(write);glDispatchCompute(1,1,1);
    /* No CPU mirror and no glFinish: growth must preserve these GPU writes. */
    check(nvn_gl_buffer_reserve(&b,mib+1,limit)==GL_NO_ERROR&&b.capacity==2*mib&&b.name==name,"first growth");
    GLint binding=0;glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,0,&binding);check((GLuint)binding==name,"SSBO binding identity");
    glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING,0,&binding);check((GLuint)binding==name,"UBO binding identity");
    glGetVertexAttribiv(0,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&binding);check((GLuint)binding==name,"vertex binding identity");
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER,1,name,mib,sizeof original);
    GLuint read=compute("#version 320 es\nlayout(local_size_x=64) in;layout(std430,binding=0) readonly buffer B {uint v[];};layout(std430,binding=1) writeonly buffer O {uint o[];};layout(std140,binding=0) uniform U {uvec4 u[16];};void main(){uint i=gl_LocalInvocationID.x;o[i]=v[i]+u[i/4u][i%4u];}");
    glUseProgram(read);glDispatchCompute(1,1,1);
    check(nvn_gl_buffer_reserve(&b,3*mib,limit)==GL_NO_ERROR&&b.capacity==4*mib,"second growth");
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);bytes_equal(name,0,original,sizeof original);bytes_equal(name,256,modified,sizeof modified);bytes_equal(name,mib,combined,sizeof combined);
    check(nvn_gl_buffer_reserve(&b,limit,limit)==GL_NO_ERROR&&b.capacity==limit&&b.name==name,"pool limit growth");
    bytes_equal(name,mib,combined,sizeof combined);
    /* Queue a small CPU update after GPU writes, using a separate store.
     * Unrelated GPU bytes and the existing bindings must remain current. */
    GLuint staging;glGenBuffers(1,&staging);uint32_t patch[3]={0xabcdef01,0x12345678,0x76543210};
    check(nvn_gl_buffer_stage_update(&b,staging,256+7*4,sizeof patch,patch)==GL_NO_ERROR,"staged partial update");
    memcpy(modified+7,patch,sizeof patch);bytes_equal(name,256,modified,sizeof modified);bytes_equal(name,mib,combined,sizeof combined);
    check(nvn_gl_buffer_stage_update(&b,staging,b.capacity-2,sizeof patch,patch)==GL_INVALID_VALUE,"staged bounds rejection");
    /* A queued consumer must retain the old contents, while a consumer after
     * the mapped patch must see new CPU bytes and all prior GPU writes. */
    uint32_t before_patch[64],after_patch[64],mapped_patch[3]={11,22,33};
    for(unsigned i=0;i<64;i++)before_patch[i]=modified[i]+original[i];
    glUseProgram(read);glBindBufferRange(GL_SHADER_STORAGE_BUFFER,1,name,2*mib,sizeof original);glDispatchCompute(1,1,1);
    check(nvn_gl_buffer_mapped_update(&b,256+9*4,sizeof mapped_patch,mapped_patch)==GL_NO_ERROR,"fenced mapped update");
    memcpy(modified+9,mapped_patch,sizeof mapped_patch);
    for(unsigned i=0;i<64;i++)after_patch[i]=modified[i]+original[i];
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER,1,name,3*mib,sizeof original);glDispatchCompute(1,1,1);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    bytes_equal(name,2*mib,before_patch,sizeof before_patch);bytes_equal(name,3*mib,after_patch,sizeof after_patch);
    bytes_equal(name,256,modified,sizeof modified);bytes_equal(name,mib,combined,sizeof combined);
    check(nvn_gl_buffer_mapped_update(&b,b.capacity-2,sizeof mapped_patch,mapped_patch)==GL_INVALID_VALUE,"mapped bounds rejection");
    glDeleteBuffers(1,&staging);
    check(nvn_gl_buffer_reserve(&b,0,limit)==GL_INVALID_VALUE&&nvn_gl_buffer_reserve(&b,limit+1,limit)==GL_INVALID_VALUE&&b.capacity==limit,"invalid ranges leave backing intact");
    uint32_t cpu_dest[64];memcpy(cpu_dest,original,sizeof cpu_dest);
    uint64_t from=(uint64_t)(uintptr_t)original,to=(uint64_t)(uintptr_t)cpu_dest;
    NvnTransferRange writer={to,sizeof cpu_dest};
    check(!nvn_transfer_cpu_copy_unchanged(from,to,sizeof original,&writer,1),"equal stale CPU mirrors cannot hide GPU destination writes");
    check(!nvn_transfer_cpu_copy_unchanged(to,from,sizeof original,&writer,1),"GPU source authority prevents copy elision");
    glBindBuffer(GL_COPY_READ_BUFFER,name);const void *mirror=glMapBufferRange(GL_COPY_READ_BUFFER,256,sizeof cpu_dest,GL_MAP_READ_BIT);
    check(mirror!=NULL,"completed GPU mirror");memcpy(cpu_dest,mirror,sizeof cpu_dest);check(glUnmapBuffer(GL_COPY_READ_BUFFER),"mirror unmap");
    check(!nvn_transfer_cpu_copy_unchanged(from,to,sizeof original,NULL,0),"completed differing mirrors still need the GPU copy");
    glBindBuffer(GL_COPY_READ_BUFFER,name);glBindBuffer(GL_COPY_WRITE_BUFFER,name);glCopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,0,256,sizeof original);
    mirror=glMapBufferRange(GL_COPY_READ_BUFFER,256,sizeof cpu_dest,GL_MAP_READ_BIT);check(mirror!=NULL,"copied mirror");memcpy(cpu_dest,mirror,sizeof cpu_dest);check(glUnmapBuffer(GL_COPY_READ_BUFFER),"copied mirror unmap");
    check(nvn_transfer_cpu_copy_unchanged(from,to,sizeof original,NULL,0),"identical authoritative copy may be elided");bytes_equal(name,256,original,sizeof original);
    check(glGetError()==GL_NO_ERROR,"GL errors");
    printf("{\"renderer\":\"%s\",\"gpu_bytes_equal\":true,\"binding_identity_preserved\":true,\"bounds_checked\":true,\"copy_elision_gpu_authority\":true,\"grows\":%llu,\"peak_scratch_bytes\":%llu}\n",glGetString(GL_RENDERER),(unsigned long long)b.grows,(unsigned long long)b.peak_scratch);
    glDeleteProgram(read);glDeleteProgram(write);glDeleteVertexArrays(1,&vao);glDeleteBuffers(1,&b.name);eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(d,surface);eglDestroyContext(d,ctx);eglTerminate(d);return 0;
}
