// INPUT: Production read-snapshot module, explicit dirty epochs and real GLES.
// OUTPUT: CPU invalidation, immutable snapshots, GPU authority and pool identity checks.
// POS: Functional fixture for GLES read bindings; tracker metadata is deterministic.
#include <EGL/egl.h>
#include "../../gfx/nvn/nvn_gl_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GL_WRITES 4096
typedef struct {uint64_t addr,size;} GlRange;
typedef struct {const uint8_t *guest;uint64_t size,serial;NvnGlBuffer backing;} GlPool;
typedef struct {GLuint buffer,texture;} GlTemp;
typedef struct {GlRange range;uint64_t pool_serial,stamp;} GlUpload;
typedef struct {GlUpload source;GLuint buffer;} GlReadSnapshot;
static struct {GlRange writes[GL_WRITES];unsigned nwrites;GlTemp temps[GL_WRITES];unsigned ntemp;GlReadSnapshot read_cache[256];uint64_t readonly_cache_hits,readonly_gpu_snapshots,readonly_snapshot_calls,readonly_snapshot_bytes;int uniform_snapshot,readonly_draw;} L;
static GlPool pool;
static uint64_t cpu_epoch=1;static int tracked=1;static unsigned uploads,protects,checks;
static _Noreturn void gl_stop(const char *what,uint64_t value){fprintf(stderr,"STOP %s %llu\n",what,(unsigned long long)value);exit(2);}
static void check(int ok,const char *what){checks++;if(!ok){fprintf(stderr,"FAIL %s GL=%#x EGL=%#x\n",what,glGetError(),eglGetError());exit(2);}}
static int overlap(GlRange a,GlRange b){return a.size&&b.size&&a.addr<b.addr+b.size&&b.addr<a.addr+a.size;}
static GlPool *gl_pool(uint64_t addr,uint64_t size,uint64_t *offset){uint64_t base=(uint64_t)(uintptr_t)pool.guest;if(addr<base||addr-base>pool.size||size>pool.size-(addr-base))return NULL;if(offset)*offset=addr-base;return &pool;}
/* GPU contents are prepared explicitly by compute below. The upload dependency
 * records use of that authoritative backing rather than replacing it from CPU. */
static GlPool *gl_upload(GlRange r,uint64_t *offset){uploads++;return gl_pool(r.addr,r.size,offset);}
static int nvn_dirty_tracked(const void *p,size_t n){(void)p;(void)n;return tracked;}
static uint64_t nvn_dirty_now(void){return cpu_epoch;}
static int nvn_dirty_since(const void *p,size_t n,uint64_t stamp){(void)p;(void)n;return cpu_epoch>stamp;}
static void nvn_dirty_protect(const void *p,size_t n){(void)p;(void)n;protects++;}
/* This fixture covers the original path with the optional uniform module off.
 * gles_uniform_device.c executes the full module and its actual GPU versions. */
static int gl_uniform_read_snapshot(GlRange r,GLuint *buffer){(void)r;(void)buffer;return 0;}
static void gl_api_memory_barrier(GLbitfield bits){glMemoryBarrier(bits);}
#include "../../gfx/nvn/nvn_gl_read_buffer.inc"
static void equals(GLuint buffer,const uint32_t *expected,size_t size){glBindBuffer(GL_COPY_READ_BUFFER,buffer);const void *p=glMapBufferRange(GL_COPY_READ_BUFFER,0,size,GL_MAP_READ_BIT);check(p&&!memcmp(p,expected,size),"buffer bytes");check(glUnmapBuffer(GL_COPY_READ_BUFFER),"unmap");}
int main(void){
 EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);check(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_ES_API),"EGL");
 EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_NONE},nc;EGLConfig config;check(eglChooseConfig(d,ca,&config,1,&nc)&&nc==1,"config");
 EGLint aa[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,2,EGL_NONE},pa[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};EGLContext context=eglCreateContext(d,config,EGL_NO_CONTEXT,aa);EGLSurface surface=eglCreatePbufferSurface(d,config,pa);check(context!=EGL_NO_CONTEXT&&surface!=EGL_NO_SURFACE&&eglMakeCurrent(d,surface,surface,context),"context");
 uint32_t *cpu;check(!posix_memalign((void **)&cpu,1024*1024,2*1024*1024),"CPU allocation");for(unsigned i=0;i<2*1024*1024/4;i++)cpu[i]=1000+i;
 pool=(GlPool){.guest=(const uint8_t *)cpu,.size=2*1024*1024,.serial=1};glGenBuffers(1,&pool.backing.name);glBindBuffer(GL_COPY_WRITE_BUFFER,pool.backing.name);glBufferData(GL_COPY_WRITE_BUFFER,pool.size,cpu,GL_DYNAMIC_DRAW);
 GlRange r={(uint64_t)(uintptr_t)cpu,256};uint64_t offset=99;GLuint first=gl_read_buffer(r,&offset);check(!offset,"snapshot offset");equals(first,cpu,256);
 GLuint same=gl_read_buffer(r,&offset);check(same==first&&L.readonly_cache_hits==1&&protects==1,"clean reuse");
 uint32_t old[64];memcpy(old,cpu,sizeof old);cpu[0]=98765;cpu_epoch++;
 GLuint changed=gl_read_buffer(r,&offset);check(changed!=first,"CPU epoch invalidation");equals(changed,cpu,256);equals(first,old,256);
 /* 1 MiB alignment and a 4096-byte displacement collide in the production
  * lookup's low bits. Exact address checks must distinguish both resources. */
 GlRange collision={r.addr+4096,r.size};GLuint other=gl_read_buffer(collision,&offset);equals(other,cpu+1024,256);GLuint replaced=gl_read_buffer(r,&offset);check(replaced!=changed,"collision replaced cache entry");equals(replaced,cpu,256);equals(changed,cpu,256);
 const char *source="#version 320 es\nlayout(local_size_x=64) in;layout(std430,binding=0) buffer B{uint v[];};uniform uint base;void main(){uint i=gl_LocalInvocationID.x;v[i]=base+i;}";
 GLuint shader=glCreateShader(GL_COMPUTE_SHADER);glShaderSource(shader,1,&source,NULL);glCompileShader(shader);GLint ok;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);check(ok,"compute compile");GLuint program=glCreateProgram();glAttachShader(program,shader);glLinkProgram(program);glGetProgramiv(program,GL_LINK_STATUS,&ok);check(ok,"compute link");glUseProgram(program);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,pool.backing.name);GLint base=glGetUniformLocation(program,"base");
 L.writes[L.nwrites++]=r;
 for(unsigned pass=0;pass<2;pass++){
  uint32_t expected[64];for(unsigned i=0;i<64;i++)expected[i]=0x12340000+pass*1000+i;
  glUniform1ui(base,expected[0]);glDispatchCompute(1,1,1);glMemoryBarrier(GL_ALL_BARRIER_BITS);
  GLuint gpu=gl_read_buffer(r,&offset);check(!offset&&gpu!=replaced,"GPU write bypasses CPU cache");equals(gpu,expected,sizeof expected);
 }
 check(uploads==2&&L.readonly_gpu_snapshots==2,"GPU backing used for each read");
 L.nwrites=0;pool.serial++;cpu[0]=0x87654321;
 GLuint reused_address=gl_read_buffer(r,&offset);check(reused_address!=replaced,"pool serial invalidation");equals(reused_address,cpu,256);
 tracked=0;cpu[0]=123;GLuint untracked=gl_read_buffer(r,&offset);cpu[0]=456;GLuint untracked_next=gl_read_buffer(r,&offset);check(untracked!=untracked_next,"untracked data never reused");equals(untracked_next,cpu,256);
 tracked=1;GlRange medium={r.addr+32,1024*1024};unsigned initial_uploads=uploads;
 GLuint bounded=gl_read_buffer(medium,&offset);check(!offset&&bounded!=pool.backing.name&&uploads==initial_uploads,"one MiB CPU binding avoids large pool update");equals(bounded,cpu+8,medium.size);
 GLuint bounded_again=gl_read_buffer(medium,&offset);check(bounded_again==bounded,"one MiB read snapshot reused");
 L.writes[L.nwrites++]=r;unsigned temps=L.ntemp;GLuint authoritative=gl_read_buffer(medium,&offset);check(authoritative==pool.backing.name&&offset==32&&L.ntemp==temps,"large GPU binding stays on authoritative backing");L.nwrites=0;
 temps=L.ntemp;GlRange large={r.addr+32,1024*1024+4};GLuint dense=gl_read_buffer(large,&offset);check(dense==pool.backing.name&&offset==32&&L.ntemp==temps,"large range fallback");
 check(glGetError()==GL_NO_ERROR,"GL errors");printf("{\"renderer\":\"%s\",\"checks\":%u,\"cpu_invalidation\":true,\"clean_reuse\":true,\"immutable_snapshots\":true,\"collision_identity\":true,\"gpu_authority\":true,\"pool_serial\":true,\"untracked_updates\":true,\"large_fallback\":true}\n",glGetString(GL_RENDERER),checks);
 glFinish();for(unsigned i=0;i<L.ntemp;i++)glDeleteBuffers(1,&L.temps[i].buffer);glDeleteBuffers(1,&pool.backing.name);glDeleteProgram(program);glDeleteShader(shader);free(cpu);eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(d,surface);eglDestroyContext(d,context);eglTerminate(d);return 0;
}
