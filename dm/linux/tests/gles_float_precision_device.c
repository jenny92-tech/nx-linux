// INPUT: Original and prepared fragment precision shaders in a real GLES context.
// OUTPUT: Measured mediump changes and exact FP32 register bit round trips.
// POS: Semantic regression for NVN float/int payloads in prepared GLES fragments.
#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"STOP %s egl=%#x gl=%#x\n",why,eglGetError(),glGetError());exit(2);}}
static char *file(const char *path){FILE *f=fopen(path,"rb");check(f!=NULL,"shader open");check(!fseek(f,0,SEEK_END),"shader seek");long n=ftell(f);check(n>0&&n<65536&&!fseek(f,0,SEEK_SET),"shader size");char *s=malloc(n+1);check(s&&fread(s,1,n,f)==(size_t)n,"shader read");s[n]=0;fclose(f);return s;}
static GLuint shader(GLenum type,const char *source){GLuint s=glCreateShader(type);glShaderSource(s,1,&source,NULL);glCompileShader(s);GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);check(ok,"shader compile");return s;}
int main(int argc,char **argv){
    check(argc==3,"shader arguments");EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);check(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_ES_API),"EGL init");
    const EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_NONE};EGLConfig config;EGLint n;check(eglChooseConfig(d,ca,&config,1,&n)&&n==1,"EGL config");
    const EGLint a[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,2,EGL_NONE},pa[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};EGLContext ctx=eglCreateContext(d,config,EGL_NO_CONTEXT,a);EGLSurface surface=eglCreatePbufferSurface(d,config,pa);check(ctx!=EGL_NO_CONTEXT&&surface!=EGL_NO_SURFACE&&eglMakeCurrent(d,surface,surface,ctx),"EGL current");
    GLuint vao,fbo,texture;glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA32UI,1,1);glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"integer framebuffer");
    GLuint vs=shader(GL_VERTEX_SHADER,"#version 320 es\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0.0,1.0);}");
    const uint32_t input[4]={0x3f800001,0x477fe000,0x47800000,0x3f800100};uint32_t actual[2][4];unsigned changed=0;
    for(unsigned variant=0;variant<2;variant++){
        char *source=file(argv[1+variant]);GLuint fs=shader(GL_FRAGMENT_SHADER,source),program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);check(ok,"shader link");glUseProgram(program);
        glUniform4uiv(glGetUniformLocation(program,"inputBits"),1,input);glUniform1f(glGetUniformLocation(program,"multiplier"),1.0f);glViewport(0,0,1,1);glDrawArrays(GL_TRIANGLES,0,3);glReadPixels(0,0,1,1,GL_RGBA_INTEGER,GL_UNSIGNED_INT,actual[variant]);check(glGetError()==GL_NO_ERROR,"register readback");glDeleteProgram(program);glDeleteShader(fs);free(source);
    }
    for(unsigned i=0;i<4;i++){changed+=actual[0][i]!=input[i];if(actual[1][i]!=input[i])fprintf(stderr,"FP32_MISMATCH word=%u got=%#x expected=%#x\n",i,actual[1][i],input[i]);check(actual[1][i]==input[i],"FP32 register round trip");}
    printf("{\"renderer\":\"%s\",\"mediump_changed_words\":%u,\"mediump_words\":[%u,%u,%u,%u],\"fp32_exact\":true,\"fp32_words\":4}\n",glGetString(GL_RENDERER),changed,actual[0][0],actual[0][1],actual[0][2],actual[0][3]);
    glDeleteShader(vs);glDeleteTextures(1,&texture);glDeleteFramebuffers(1,&fbo);glDeleteVertexArrays(1,&vao);eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(d,surface);eglDestroyContext(d,ctx);eglTerminate(d);return 0;
}
