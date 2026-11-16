// INPUT: Production resource adapter, deterministic page epochs and real GLES.
// OUTPUT: Exact texture values, immutable versions, bounded reuse and GPU authority.
// POS: GPU regression for the generic resource-layer GLES adapter.
#include <EGL/egl.h>
#include "../../gfx/nvn/nvn_gl_buffer.h"
#include "../../gfx/nvn/nvn_transfer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GL_WRITES 64
#define GL_RESOURCE_ENTRIES 4
#define GL_RESOURCE_BUDGET 32768ull
typedef NvnTransferRange GlRange;
typedef struct {const uint8_t *guest;uint64_t size,serial;NvnGlBuffer backing;} GlPool;
typedef struct {GLuint buffer,texture;} GlTemp;
typedef struct {GlRange range;uint64_t pool_serial,stamp;} GlUpload;
typedef struct {GlUpload source;GLuint texture;GLenum internal,format,type;unsigned bpp,width,height;uint64_t bytes,age;int pinned;} GlResource;
static struct {GlRange writes[GL_WRITES];unsigned nwrites;GlTemp temps[GL_WRITES];unsigned ntemp;GlResource resources[GL_RESOURCE_ENTRIES];uint64_t resource_bytes,resource_age,resource_hits,resource_partial,resource_evictions,snapshot_calls[2],snapshot_bytes[2];unsigned max_texture2d,max_tex;} L;
static GlPool pool;static uint64_t epoch=1,pages[32];static int tracked=1;static unsigned checks,fixture_uploads;
static _Noreturn void gl_stop(const char *what,uint64_t value){fprintf(stderr,"STOP %s %llu\n",what,(unsigned long long)value);exit(2);}
static int gl_filter(const char *what,uint64_t value){gl_stop(what,value);}
static void check(int ok,const char *what){checks++;if(!ok){fprintf(stderr,"FAIL %s GL=%#x EGL=%#x\n",what,glGetError(),eglGetError());exit(2);}}
static int overlap(GlRange a,GlRange b){return a.size&&b.size&&a.addr<b.addr+b.size&&b.addr<a.addr+a.size;}
static GlPool *gl_pool(uint64_t addr,uint64_t size,uint64_t *off){uint64_t base=(uint64_t)(uintptr_t)pool.guest;if(addr<base||addr-base>pool.size||size>pool.size-(addr-base))return NULL;if(off)*off=addr-base;return &pool;}
static int nvn_dirty_tracked(const void *p,size_t n){return tracked&&gl_pool((uint64_t)(uintptr_t)p,n,NULL);}
static uint64_t nvn_dirty_now(void){return epoch;}
static void nvn_dirty_protect(const void *p,size_t n){(void)p;(void)n;}
static int nvn_dirty_since(const void *p,size_t n,uint64_t stamp){
    size_t first=((const uint8_t *)p-pool.guest)/4096,last=((const uint8_t *)p-pool.guest+n-1)/4096;
    for(size_t i=first;i<=last;i++)if(pages[i]>stamp)return 1;return 0;
}
static int nvn_dirty_next_range(const void *p,size_t n,uint64_t stamp,size_t *cursor,size_t *off,size_t *len){
    size_t base=(const uint8_t *)p-pool.guest,at=*cursor;
    while(at<n&&pages[(base+at)/4096]<=stamp){size_t next=((base+at)/4096+1)*4096-base;at=next<n?next:n;}
    if(at==n)return 0;*off=at;
    while(at<n&&pages[(base+at)/4096]>stamp){size_t next=((base+at)/4096+1)*4096-base;at=next<n?next:n;}
    *len=at-*off;*cursor=at;return 1;
}
static void upload(void *context,uint64_t addr,uint64_t size){(void)context;glBufferSubData(GL_COPY_WRITE_BUFFER,addr-(uint64_t)(uintptr_t)pool.guest,size,(const void *)(uintptr_t)addr);}
static GlPool *gl_upload(GlRange r,uint64_t *offset){
    GlPool *p=gl_pool(r.addr,r.size,offset);if(!p)gl_stop("fixture pool",r.addr);fixture_uploads++;
    glBindBuffer(GL_COPY_WRITE_BUFFER,p->backing.name);check(nvn_transfer_visit(r.addr,r.size,L.writes,L.nwrites,0,NULL,upload,NULL)!=UINT64_MAX,"fixture CPU upload");return p;
}
static void gl_uniform_flush_range(GlRange r){(void)r;}
#include "../../gfx/nvn/nvn_gl_resource.inc"
static void finish(void){glFinish();for(unsigned i=0;i<GL_RESOURCE_ENTRIES;i++)L.resources[i].pinned=0;L.nwrites=0;}
static void changed(size_t first,size_t size){epoch++;for(size_t i=first/4096;i<=(first+size-1)/4096;i++)pages[i]=epoch;}
static void texture_equals(GLuint texture,const void *expected,size_t bytes,int rgba){
    unsigned bpp=rgba==2?16:4,texels=bytes/bpp,width=texels<2048?texels:2048,height=(texels+width-1)/width;uint8_t *got=malloc((size_t)width*height*bpp);check(got!=NULL,"read allocation");
    GLuint fbo;glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"read framebuffer");glReadPixels(0,0,width,height,rgba==2?GL_RGBA_INTEGER:rgba?GL_RGBA:GL_RED_INTEGER,rgba==1?GL_UNSIGNED_BYTE:GL_UNSIGNED_INT,got);
    check(glGetError()==GL_NO_ERROR&&!memcmp(got,expected,bytes),"texture bytes");free(got);glDeleteFramebuffers(1,&fbo);
}
int main(void){
    EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);check(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_ES_API),"EGL");
    EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_NONE},nc;EGLConfig config;check(eglChooseConfig(d,ca,&config,1,&nc)&&nc==1,"config");
    EGLint aa[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,2,EGL_NONE},pa[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};EGLContext context=eglCreateContext(d,config,EGL_NO_CONTEXT,aa);EGLSurface surface=eglCreatePbufferSurface(d,config,pa);check(context!=EGL_NO_CONTEXT&&surface!=EGL_NO_SURFACE&&eglMakeCurrent(d,surface,surface,context),"context");
    uint32_t *cpu;check(!posix_memalign((void **)&cpu,4096,128*1024),"CPU allocation");for(unsigned i=0;i<128*1024/4;i++)cpu[i]=1000+i;
    pool=(GlPool){.guest=(uint8_t *)cpu,.size=128*1024,.serial=1};for(unsigned i=0;i<32;i++)pages[i]=epoch;
    L.max_texture2d=4096;L.max_tex=16;glGenBuffers(1,&pool.backing.name);glBindBuffer(GL_COPY_WRITE_BUFFER,pool.backing.name);glBufferData(GL_COPY_WRITE_BUFFER,pool.size,cpu,GL_DYNAMIC_DRAW);
    GlRange r={(uint64_t)(uintptr_t)cpu,8192};GLuint first=gl_resource_snapshot(r,GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,4,0);texture_equals(first,cpu,r.size,0);
    check(fixture_uploads==0,"CPU texture upload bypasses large pool");
    GLuint same=gl_resource_snapshot(r,GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,4,0);check(same==first&&L.resource_hits==1,"clean reuse");
    uint32_t old[2048];memcpy(old,cpu,sizeof old);cpu[0]=0x87654321;changed(0,4);
    GLuint newer=gl_resource_snapshot(r,GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,4,0);check(newer!=first,"immutable in-flight version");texture_equals(first,old,sizeof old,0);texture_equals(newer,cpu,r.size,0);
    finish();cpu[1024]=77;changed(4096,4);uint64_t before=L.snapshot_bytes[0];
    GLuint partial=gl_resource_snapshot(r,GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,4,0);check(partial==newer&&L.snapshot_bytes[0]-before==4096&&L.resource_partial==1,"one-page partial update");texture_equals(partial,cpu,r.size,0);
    GLuint rgba=gl_resource_snapshot(r,GL_RGBA8,GL_RGBA,GL_UNSIGNED_BYTE,4,1);check(rgba!=partial,"format identity");texture_equals(rgba,cpu,r.size,1);
    for(unsigned i=1;i<5;i++){GlRange other={r.addr+i*8192,r.size};gl_resource_snapshot(other,GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,4,0);check(L.resource_bytes<=GL_RESOURCE_BUDGET,"bounded cache with pinned fallback");}
    check(L.ntemp>0,"pinned budget fallback");texture_equals(partial,cpu,r.size,0);
    finish();gl_resource_forget(pool.serial);check(L.resource_bytes==0,"pool release");pool.serial++;
    /* Unaligned range edges exercise partial rows and clipping at both ends. */
    GlRange edges={r.addr+4,24576};GLuint edge=gl_resource_snapshot(edges,GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,4,0);finish();
    cpu[1024]=1234;changed(4096,4096);before=L.snapshot_bytes[0];GLuint edge2=gl_resource_snapshot(edges,GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,4,0);
    check(edge==edge2&&L.snapshot_bytes[0]-before==4096,"partial row span");texture_equals(edge2,cpu+1,edges.size,0);
    finish();uint64_t hits=L.resource_hits,previous_serial=pool.serial;pool.serial++;cpu[1]^=0xabcdef;
    GLuint rebuilt=gl_resource_snapshot(edges,GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,4,0);
    check(L.resource_hits==hits,"same address new pool identity");texture_equals(rebuilt,cpu+1,edges.size,0);
    finish();gl_resource_forget(previous_serial);gl_resource_forget(pool.serial);check(L.resource_bytes==0,"all pool versions released");
    GlRange tail={r.addr+4,(2048+7)*4};GLuint tail_texture=gl_resource_snapshot(tail,GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,4,0);texture_equals(tail_texture,cpu+1,tail.size,0);finish();
    cpu[2055]^=17;changed(8220,4);before=L.snapshot_bytes[0];GLuint tail_update=gl_resource_snapshot(tail,GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,4,0);
    check(tail_texture==tail_update&&L.snapshot_bytes[0]-before==32,"partial final row and range clipping");texture_equals(tail_update,cpu+1,tail.size,0);finish();gl_resource_forget(pool.serial);
    GlRange wide={r.addr+4,2160};GLuint wide_texture=gl_resource_snapshot(wide,GL_RGBA32UI,GL_RGBA_INTEGER,GL_UNSIGNED_INT,16,1);texture_equals(wide_texture,cpu+1,wide.size,2);finish();
    cpu[7]^=39;changed(28,4);GLuint wide_update=gl_resource_snapshot(wide,GL_RGBA32UI,GL_RGBA_INTEGER,GL_UNSIGNED_INT,16,1);check(wide_texture==wide_update,"sixteen-byte texel update");texture_equals(wide_update,cpu+1,wide.size,2);finish();gl_resource_forget(pool.serial);
    const char *source="#version 320 es\nlayout(local_size_x=64) in;layout(std430,binding=0) buffer B{uint v[];};uniform uint base;void main(){uint i=gl_LocalInvocationID.x;v[i]=base+i;}";
    GLuint shader=glCreateShader(GL_COMPUTE_SHADER);glShaderSource(shader,1,&source,NULL);glCompileShader(shader);GLint ok;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);check(ok,"compute compile");GLuint program=glCreateProgram();glAttachShader(program,shader);glLinkProgram(program);glGetProgramiv(program,GL_LINK_STATUS,&ok);check(ok,"compute link");glUseProgram(program);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,pool.backing.name);
    GlRange gpu={r.addr,256};L.writes[L.nwrites++]=gpu;GLuint gpu_textures[2];uint32_t expected[2][64];
    for(unsigned pass=0;pass<2;pass++){
        for(unsigned i=0;i<64;i++)expected[pass][i]=0x12340000+pass*1000+i;
        glUniform1ui(glGetUniformLocation(program,"base"),expected[pass][0]);glDispatchCompute(1,1,1);glMemoryBarrier(GL_ALL_BARRIER_BITS);
        gpu_textures[pass]=gl_resource_snapshot(gpu,GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,4,0);texture_equals(gpu_textures[pass],expected[pass],256,0);
    }
    check(gpu_textures[0]!=gpu_textures[1],"repeated GPU writes are never CPU-cache hits");texture_equals(gpu_textures[0],expected[0],256,0);finish();
    /* Reuse storage across real submission completion, but never mistake a
     * repeated GPU write for a CPU-epoch cache hit. */
    GlRange mixed={r.addr,8192};L.writes[L.nwrites++]=gpu;
    uint32_t mixed_expected[2048];memcpy(mixed_expected,cpu,sizeof mixed_expected);memcpy(mixed_expected,expected[1],256);
    GLuint mixed_texture=gl_resource_snapshot(mixed,GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,4,0);texture_equals(mixed_texture,mixed_expected,sizeof mixed_expected,0);finish();
    for(unsigned pass=0;pass<2;pass++){
        for(unsigned i=0;i<64;i++)mixed_expected[i]=0x98760000+pass*1000+i;
        glUniform1ui(glGetUniformLocation(program,"base"),mixed_expected[0]);glDispatchCompute(1,1,1);glMemoryBarrier(GL_ALL_BARRIER_BITS);L.writes[L.nwrites++]=gpu;
        if(pass){cpu[300]=0x7654321;changed(1200,4);mixed_expected[300]=cpu[300];}
        before=L.snapshot_bytes[0];GLuint next=gl_resource_snapshot(mixed,GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,4,0);
        check(next==mixed_texture,"unpinned GPU version reuses bounded storage");
        check(L.snapshot_bytes[0]-before==(pass?4096:256),"CPU dirty and GPU-written spans copied exactly");
        texture_equals(next,mixed_expected,sizeof mixed_expected,0);finish();
    }
    /* A GPU update inside a 16-byte texel needs the authoritative byte merge;
     * rounding a direct CPU upload would overwrite the GPU-owned component. */
    uint32_t gpu_word=0xc0ffee;glBindBuffer(GL_COPY_WRITE_BUFFER,pool.backing.name);glBufferSubData(GL_COPY_WRITE_BUFFER,4,4,&gpu_word);
    L.writes[L.nwrites++]=(GlRange){r.addr+4,4};memcpy(mixed_expected,cpu,sizeof mixed_expected);mixed_expected[1]=gpu_word;
    unsigned uploads=fixture_uploads;GLuint merged=gl_resource_snapshot(mixed,GL_RGBA32UI,GL_RGBA_INTEGER,GL_UNSIGNED_INT,16,1);
    check(fixture_uploads==uploads+1,"partial GPU texel uses authoritative merge");texture_equals(merged,mixed_expected,sizeof mixed_expected,2);finish();
    tracked=0;GLuint u=gl_resource_snapshot(gpu,GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,4,0);cpu[0]=999;GLuint u2=gl_resource_snapshot(gpu,GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,4,0);check(u!=u2,"untracked range fallback");texture_equals(u2,cpu,256,0);
    finish();gl_resource_forget(pool.serial);tracked=1;
    GLuint ring[3];
    for(unsigned i=0;i<3;i++)ring[i]=gl_resource_snapshot((GlRange){r.addr+i*8192,8192},GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,4,0);
    finish();uint64_t evictions=L.resource_evictions;
    for(unsigned i=0;i<3;i++){
        cpu[i*2048+17]^=0x1234567;changed(i*8192+68,4);before=L.snapshot_bytes[0];
        GLuint next=gl_resource_snapshot((GlRange){r.addr+i*8192,8192},GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,4,0);
        check(next==ring[i]&&L.resource_evictions==evictions,"three rotating resources retain storage");
        check(L.snapshot_bytes[0]-before==4096,"rotating resource patches one dirty page");texture_equals(next,cpu+i*2048,8192,0);finish();
    }
    check(glGetError()==GL_NO_ERROR,"final GL errors");printf("{\"renderer\":\"%s\",\"checks\":%u,\"partial_update\":true,\"immutable_versions\":true,\"gpu_authority\":true,\"format_identity\":true,\"budget\":true}\n",glGetString(GL_RENDERER),checks);
    finish();for(unsigned i=0;i<GL_RESOURCE_ENTRIES;i++)gl_resource_discard(&L.resources[i]);for(unsigned i=0;i<L.ntemp;i++)glDeleteTextures(1,&L.temps[i].texture);glDeleteBuffers(1,&pool.backing.name);glDeleteProgram(program);glDeleteShader(shader);free(cpu);eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(d,surface);eglDestroyContext(d,context);eglTerminate(d);return 0;
}
