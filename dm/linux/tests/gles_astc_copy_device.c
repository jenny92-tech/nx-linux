// INPUT: Existing ASTC block/RGBA fixtures, production copy module and real GLES.
// OUTPUT: Linear/sRGB decoded arrays, exact mip crops and untouched layer bytes.
// POS: GPU semantic regression for the prepared material copy path.
#include <EGL/egl.h>
#include "../../gfx/nvn/nvn_gl_astc_copy.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static unsigned checks;
static void check(int ok,const char *why){checks++;if(!ok){fprintf(stderr,"STOP %s egl=%#x gl=%#x\n",why,eglGetError(),glGetError());exit(2);}}
static void file(const char *root,const char *name,void *out,size_t n){char path[2048];snprintf(path,sizeof path,"%s/%s",root,name);FILE *f=fopen(path,"rb");check(f&&fread(out,1,n,f)==n&&fgetc(f)==EOF,"fixture");fclose(f);}
static void equal(const uint8_t *got,const uint8_t *expected,size_t n){for(size_t i=0;i<n;i++)check(abs((int)got[i]-expected[i])<=1,"decoded bytes");}
int main(int argc,char **argv){
    check(argc==2,"fixture argument");EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);check(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_ES_API),"EGL init");
    const EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_NONE};EGLConfig config;EGLint n;
    check(eglChooseConfig(d,ca,&config,1,&n)&&n==1,"EGL config");const EGLint a[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,2,EGL_NONE},pa[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};
    EGLContext ctx=eglCreateContext(d,config,EGL_NO_CONTEXT,a);EGLSurface surface=eglCreatePbufferSurface(d,config,pa);check(ctx!=EGL_NO_CONTEXT&&surface!=EGL_NO_SURFACE&&eglMakeCurrent(d,surface,surface,ctx),"EGL current");
    NvnGlAstcCopy copy={0};GLuint vao,fbo;glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ALIGNMENT,1);GLenum attachment=GL_COLOR_ATTACHMENT0;glDrawBuffers(1,&attachment);
    for(unsigned srgb=0;srgb<2;srgb++){
        uint8_t blocks[64],golden[140],pixels[140],initial[280],expected[24];
        file(argv[1],srgb?"srgb.blocks":"linear.blocks",blocks,sizeof blocks);file(argv[1],srgb?"srgb.rgba":"linear.rgba",golden,sizeof golden);
        for(unsigned i=0;i<sizeof initial;i++)initial[i]=(uint8_t)(17+(i%4)*20);
        GLuint source,dest;glGenTextures(1,&source);glBindTexture(GL_TEXTURE_2D,source);GLenum compressed=srgb?0x93d0:0x93b0;
        glTexStorage2D(GL_TEXTURE_2D,2,compressed,7,5);glCompressedTexSubImage2D(GL_TEXTURE_2D,0,0,0,7,5,compressed,sizeof blocks,blocks);glCompressedTexSubImage2D(GL_TEXTURE_2D,1,0,0,3,2,compressed,16,blocks);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glGenTextures(1,&dest);glBindTexture(GL_TEXTURE_2D_ARRAY,dest);glTexStorage3D(GL_TEXTURE_2D_ARRAY,2,srgb?GL_SRGB8_ALPHA8:GL_RGBA8,7,5,2);
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY,0,0,0,0,7,5,2,GL_RGBA,GL_UNSIGNED_BYTE,initial);glTexSubImage3D(GL_TEXTURE_2D_ARRAY,1,0,0,0,3,2,2,GL_RGBA,GL_UNSIGNED_BYTE,initial);check(glGetError()==GL_NO_ERROR,"ASTC fixture upload");
        glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,dest,0,1);check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"array framebuffer");
        check(nvn_gl_astc_copy_draw(&copy,source,15,0,0,0,0,0,7,5)==GL_NO_ERROR,"ASTC decode to material array");glReadPixels(0,0,7,5,GL_RGBA,GL_UNSIGNED_BYTE,pixels);equal(pixels,golden,sizeof golden);
        glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,dest,0,0);glReadPixels(0,0,7,5,GL_RGBA,GL_UNSIGNED_BYTE,pixels);equal(pixels,initial,140);
        glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,dest,1,1);check(nvn_gl_astc_copy_draw(&copy,source,15,1,1,1,0,0,2,1)==GL_NO_ERROR,"mip crop decode");
        memcpy(expected,initial,sizeof expected);for(unsigned x=0;x<2;x++)memcpy(expected+x*4,golden+(7+1+x)*4,4);
        glReadPixels(0,0,3,2,GL_RGBA,GL_UNSIGNED_BYTE,pixels);equal(pixels,expected,sizeof expected);check(glGetError()==GL_NO_ERROR,"copy readback");glDeleteTextures(1,&source);glDeleteTextures(1,&dest);
    }
    check(nvn_gl_astc_copy_draw(&copy,0,15,0,0,0,0,0,7,5)==GL_INVALID_VALUE,"invalid source rejected");
    printf("{\"renderer\":\"%s\",\"checks\":%u,\"linear_srgb\":true,\"array_layers\":true,\"mip_crop\":true,\"source_kept_compressed\":true}\n",glGetString(GL_RENDERER),checks);
    glDeleteProgram(copy.program);glDeleteFramebuffers(1,&fbo);glDeleteVertexArrays(1,&vao);eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(d,surface);eglDestroyContext(d,ctx);eglTerminate(d);return 0;
}
