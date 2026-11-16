// INPUT: Production ASTC coherence code, Tegra layouts and real GLES image copies.
// OUTPUT: CPU block edits reach sampling without replacing neighboring GPU contents.
// POS: Regression for compressed textures updated by both guest CPU and GPU transfers.
#include <EGL/egl.h>
#include "../../gfx/nvn/nvn_gl_astc_copy.h"
#include "../../gfx/nvn/nvn_vk_snapshot.h"
#include "../../gfx/nvn/nvn_obj.h"
#include "../../gfx/tex/nvn_deswizzle.h"
#include <string.h>
typedef struct {tex_t source;GLuint name;GLenum target,internal;uint8_t *cpu_snapshot;size_t cpu_snapshot_size;uint64_t stamp,version;} GlTexture;
static struct {uint64_t astc_baseline_bytes,astc_refreshes,astc_refresh_blocks,uploads;} L;
static uint64_t generation=1;static unsigned checks;
static void check(int ok,const char *why){checks++;if(!ok){fprintf(stderr,"STOP %s gl=%#x\n",why,glGetError());exit(2);}}
static void gl_stop(const char *why,uint64_t value){fprintf(stderr,"STOP %s %llu\n",why,(unsigned long long)value);exit(2);}
static unsigned gl_dim(unsigned n,unsigned l){n>>=l;return n?n:1;}
static uint64_t nvn_dirty_now(void){return generation;}
static void nvn_dirty_protect(const void *p,size_t n){(void)p;(void)n;}
#include "../../gfx/nvn/nvn_gl_astc_stream.inc"
static void solid(uint8_t *out,unsigned count,unsigned r,unsigned g,unsigned b){
    const uint8_t header[8]={0xfc,0xfd,0xff,0xff,0xff,0xff,0xff,0xff};
    for(unsigned i=0;i<count;i++){memcpy(out+i*16,header,8);uint16_t c[4]={r*257,g*257,b*257,65535};memcpy(out+i*16+8,c,8);}
}
static void pixels(GLuint texture,GLuint dest,GLuint fbo,NvnGlAstcCopy *copy,unsigned level,unsigned width,unsigned height,unsigned changed){
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,dest,0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"read target");
    check(!nvn_gl_astc_copy_draw(copy,texture,15,level,0,0,0,0,width,height),"sample streamed ASTC");
    uint8_t got[7*5*4];glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,got);
    for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++){
        const uint8_t *p=got+(y*width+x)*4;int cpu=changed&&x>=4&&y>=4;
        check(p[0]==(cpu?255:0)&&p[1]==(cpu?255:0)&&p[2]==(cpu?0:255)&&p[3]==255,"CPU edit and preserved GPU blocks");
    }
}
int main(void){
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);check(eglInitialize(display,NULL,NULL)&&eglBindAPI(EGL_OPENGL_ES_API),"EGL init");
    EGLConfig config;EGLint n;const EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_NONE};
    check(eglChooseConfig(display,ca,&config,1,&n)&&n==1,"EGL config");const EGLint pa[]={EGL_WIDTH,8,EGL_HEIGHT,8,EGL_NONE},a[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,2,EGL_NONE};
    EGLSurface surface=eglCreatePbufferSurface(display,config,pa);EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,a);check(eglMakeCurrent(display,surface,surface,context),"EGL current");
    GLuint vao,fbo,dest;glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenFramebuffers(1,&fbo);glGenTextures(1,&dest);glBindTexture(GL_TEXTURE_2D,dest);glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,7,5);NvnGlAstcCopy copy={0};
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ALIGNMENT,1);
    for(unsigned srgb=0;srgb<2;srgb++){
        GlTexture t={.source={.d={.target=NVN_TEXTURE_TARGET_2D,.format=srgb?135:121,.width=7,.height=5,.depth=1,.levels=2},.serial=srgb+1},.target=GL_TEXTURE_2D,.internal=srgb?0x93d0:0x93b0,.version=1};
        nvn_tex_layout(&t.source.d,&t.source.L);t.source.mem=calloc(1,t.source.L.size);check(t.source.mem!=NULL,"storage allocation");uint8_t red[64],blue[64],next[64];solid(red,4,255,0,0);solid(blue,4,0,0,255);
        check(!nvn_swizzle(&t.source.d,0,0,red,32,t.source.mem)&&!nvn_swizzle(&t.source.d,1,0,red,16,t.source.mem),"CPU baseline layout");uint8_t *baseline=malloc(t.source.L.size);memcpy(baseline,t.source.mem,t.source.L.size);gl_astc_baseline(&t,baseline);
        GLuint gpu;glGenTextures(1,&gpu);glBindTexture(GL_TEXTURE_2D,gpu);glTexStorage2D(GL_TEXTURE_2D,2,t.internal,7,5);glCompressedTexSubImage2D(GL_TEXTURE_2D,0,0,0,7,5,t.internal,64,blue);glCompressedTexSubImage2D(GL_TEXTURE_2D,1,0,0,3,2,t.internal,16,blue);
        glGenTextures(1,&t.name);glBindTexture(GL_TEXTURE_2D,t.name);glTexStorage2D(GL_TEXTURE_2D,2,t.internal,7,5);glCopyImageSubData(gpu,GL_TEXTURE_2D,0,0,0,0,t.name,GL_TEXTURE_2D,0,0,0,0,7,5,1);glCopyImageSubData(gpu,GL_TEXTURE_2D,1,0,0,0,t.name,GL_TEXTURE_2D,1,0,0,0,3,2,1);check(!glGetError(),"GPU copy");
        memcpy(next,red,64);solid(next+48,1,255,255,0);check(!nvn_swizzle(&t.source.d,0,0,next,32,t.source.mem),"one CPU block edit");generation++;
        uint64_t before=L.astc_refresh_blocks;gl_refresh_astc_cpu(&t,&t.source);check(L.astc_refresh_blocks==before+1&&t.version==2,"one exact block update");
        pixels(t.name,dest,fbo,&copy,0,7,5,1);pixels(t.name,dest,fbo,&copy,1,3,2,0);
        generation++;gl_refresh_astc_cpu(&t,&t.source);check(L.astc_refresh_blocks==before+1&&t.version==2&&t.stamp==generation,"false page dirtiness preserves GPU contents");pixels(t.name,dest,fbo,&copy,0,7,5,1);
        gl_astc_baseline(&t,NULL);check(!L.astc_baseline_bytes,"baseline release");free(t.source.mem);glDeleteTextures(1,&t.name);glDeleteTextures(1,&gpu);
    }
    check(!glGetError(),"final GL errors");printf("{\"renderer\":\"%s\",\"checks\":%u,\"gpu_blocks_preserved\":true,\"cpu_block_updated\":true,\"linear_srgb\":true,\"mip_edge\":true}\n",glGetString(GL_RENDERER),checks);
    glDeleteProgram(copy.program);glDeleteTextures(1,&dest);glDeleteFramebuffers(1,&fbo);glDeleteVertexArrays(1,&vao);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);return 0;
}
