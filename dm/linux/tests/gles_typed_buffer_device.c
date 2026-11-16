// INPUT: Prepared typed-buffer GLSL and a real GLES 3.2 context.
// OUTPUT: Decoded CPU/GPU-written bytes compared with native texture sampling.
// POS: GPU regression for the preprocessor/runtime buffer-format contract.
#include <EGL/egl.h>
#include "../../gfx/nvn/nvn_gl_buffer_sample.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
static void check(int ok,const char *what){checks++;if(!ok){fprintf(stderr,"FAIL %s GL=%#x EGL=%#x\n",what,glGetError(),eglGetError());exit(2);}}
static GLuint shader(GLenum type,const char *source){GLuint s=glCreateShader(type);glShaderSource(s,1,&source,NULL);glCompileShader(s);GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){char log[4096];glGetShaderInfoLog(s,sizeof log,NULL,log);fprintf(stderr,"%s\n",log);}check(ok,"shader compile");return s;}
static GLuint graphics(const char *path){
    FILE *f=fopen(path,"rb");check(f!=NULL,"shader file");fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);char *text=calloc(1,(size_t)n+1);check(text&&fread(text,1,n,f)==(size_t)n,"shader read");fclose(f);
    GLuint v=shader(GL_VERTEX_SHADER,"#version 320 es\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}"),s=shader(GL_FRAGMENT_SHADER,text),p=glCreateProgram();free(text);glAttachShader(p,v);glAttachShader(p,s);glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){char log[4096];glGetProgramInfoLog(p,sizeof log,NULL,log);fprintf(stderr,"%s\n",log);}check(ok,"program link");glDeleteShader(v);glDeleteShader(s);return p;
}
typedef struct {GLenum internal,format,type;unsigned channels,bytes;int scalar;} Format;
#define F(i,f,t,c,b,s) {GL_##i,GL_##f,GL_##t,c,b,s}
static const Format formats[]={
 F(R8,RED,UNSIGNED_BYTE,1,1,0),F(RG8,RG,UNSIGNED_BYTE,2,1,0),F(RGBA8,RGBA,UNSIGNED_BYTE,4,1,0),
 F(R8UI,RED_INTEGER,UNSIGNED_BYTE,1,1,2),F(RG8UI,RG_INTEGER,UNSIGNED_BYTE,2,1,2),F(RGBA8UI,RGBA_INTEGER,UNSIGNED_BYTE,4,1,2),
 F(R16F,RED,HALF_FLOAT,1,2,0),F(RG16F,RG,HALF_FLOAT,2,2,0),F(RGBA16F,RGBA,HALF_FLOAT,4,2,0),
 F(R32F,RED,FLOAT,1,4,0),F(RG32F,RG,FLOAT,2,4,0),F(RGB32F,RGB,FLOAT,3,4,0),F(RGBA32F,RGBA,FLOAT,4,4,0),
 F(R32UI,RED_INTEGER,UNSIGNED_INT,1,4,2),F(RG32UI,RG_INTEGER,UNSIGNED_INT,2,4,2),F(RGB32UI,RGB_INTEGER,UNSIGNED_INT,3,4,2),F(RGBA32UI,RGBA_INTEGER,UNSIGNED_INT,4,4,2),
 F(R32I,RED_INTEGER,INT,1,4,1),F(RG32I,RG_INTEGER,INT,2,4,1),F(RGB32I,RGB_INTEGER,INT,3,4,1),F(RGBA32I,RGBA_INTEGER,INT,4,4,1)};
int main(int argc,char **argv){
    check(argc==4,"three prepared shader paths");EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);check(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_ES_API),"EGL");EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_NONE},nc;EGLConfig c;check(eglChooseConfig(d,ca,&c,1,&nc)&&nc==1,"config");EGLint aa[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,2,EGL_NONE},pa[]={EGL_WIDTH,8,EGL_HEIGHT,8,EGL_NONE};EGLContext context=eglCreateContext(d,c,EGL_NO_CONTEXT,aa);EGLSurface surface=eglCreatePbufferSurface(d,c,pa);check(eglMakeCurrent(d,surface,surface,context),"context");
    GLuint programs[3]={graphics(argv[1]),graphics(argv[2]),graphics(argv[3])},output,fbo,buffer,vao;
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA32UI,8,8);glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"output");glViewport(0,0,8,8);glGenBuffers(1,&buffer);
    GLuint cs=shader(GL_COMPUTE_SHADER,"#version 320 es\nlayout(local_size_x=64)in;layout(std430,binding=1)buffer B{uint d[];};void main(){d[gl_LocalInvocationID.x]^=0x00010001u;}"),compute=glCreateProgram();glAttachShader(compute,cs);glLinkProgram(compute);GLint linked;glGetProgramiv(compute,GL_LINK_STATUS,&linked);check(linked,"compute link");glDeleteShader(cs);
    for(unsigned test=0;test<sizeof formats/sizeof *formats;test++){
        const Format *f=&formats[test];GLuint program=programs[f->scalar];unsigned bpp=f->channels*f->bytes;GLint layout[3];check(nvn_gl_sampler_layout(f->format,f->type,bpp,f->scalar,layout),"runtime format layout");uint8_t data[256]={0};
        for(unsigned j=0;j<8*f->channels;j++){uint32_t v;if(f->bytes==1)v=(j*43+17)&255;else if(f->bytes==2)v=0x3800+(j%4)*0x400;else if(f->type==GL_FLOAT){float value=(int)(j%7)-3.25f;memcpy(&v,&value,4);}else v=0xf1234567+j*37;memcpy(data+3+j*f->bytes,&v,f->bytes);}
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof data,data,GL_DYNAMIC_COPY);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,buffer);
        GLuint reference;glActiveTexture(GL_TEXTURE0);glGenTextures(1,&reference);glBindTexture(GL_TEXTURE_2D,reference);glTexStorage2D(GL_TEXTURE_2D,1,f->internal,8,1);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        for(unsigned gpu=0;gpu<2;gpu++){
            if(gpu){glUseProgram(compute);glDispatchCompute(1,1,1);glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);for(unsigned j=0;j<sizeof data;j+=4){uint32_t word;memcpy(&word,data+j,4);word^=0x00010001;memcpy(data+j,&word,4);}}
            glBindTexture(GL_TEXTURE_2D,reference);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,8,1,f->format,f->type,data+3);glUseProgram(program);glUniform4i(glGetUniformLocation(program,"dm_BufferLayout_16"),3,layout[0],layout[1],layout[2]);glUniform1i(glGetUniformLocation(program,"dm_BufferTexels_16"),8);
            for(int index=0;index<8;index++){glUniform1i(glGetUniformLocation(program,"index"),index);glDrawArrays(GL_TRIANGLES,0,3);uint32_t value[4];glReadPixels(0,0,1,1,GL_RGBA_INTEGER,GL_UNSIGNED_INT,value);check(glGetError()==GL_NO_ERROR&&value[0]==1&&value[1]==1&&value[2]==1&&value[3]==1,"typed data matches native texture");}
        }
        glDeleteTextures(1,&reference);
    }
    check(!nvn_gl_sampler_layout(GL_RED,GL_UNSIGNED_BYTE,1,2,(GLint[3]){0}),"numeric type mismatch rejected");printf("{\"renderer\":\"%s\",\"checks\":%u,\"formats\":%zu,\"unaligned_bytes\":true,\"gpu_written_bytes\":true,\"native_texture_oracle\":true}\n",glGetString(GL_RENDERER),checks,sizeof formats/sizeof *formats);
    for(unsigned i=0;i<3;i++)glDeleteProgram(programs[i]);glDeleteProgram(compute);glDeleteTextures(1,&output);glDeleteFramebuffers(1,&fbo);glDeleteBuffers(1,&buffer);glDeleteVertexArrays(1,&vao);eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(d,surface);eglDestroyContext(d,context);eglTerminate(d);return 0;
}
