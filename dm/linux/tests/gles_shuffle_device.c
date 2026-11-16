// INPUT: Prepared shared-memory shuffle GLSL and a real GLES 3.2 context.
// OUTPUT: Exact cross-lane NaN payloads and 32-lane integer reductions.
// POS: Semantic GPU regression for the offline reconverged shuffle lowering.
#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"STOP %s egl=%#x gl=%#x\n",why,eglGetError(),glGetError());exit(2);}}
int main(int argc,char **argv){
    check(argc==2,"shader argument");FILE *f=fopen(argv[1],"rb");check(f!=NULL,"shader open");
    check(!fseek(f,0,SEEK_END),"shader seek");long bytes=ftell(f);check(bytes>0&&bytes<65536&&!fseek(f,0,SEEK_SET),"shader length");
    char *source=malloc(bytes+1);check(source&&fread(source,1,bytes,f)==(size_t)bytes,"shader read");source[bytes]=0;fclose(f);
    EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);check(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_ES_API),"EGL init");
    const EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_NONE};EGLConfig config;EGLint n;
    check(eglChooseConfig(d,ca,&config,1,&n)&&n==1,"EGL config");
    const EGLint a[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,2,EGL_NONE},pa[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};
    EGLContext c=eglCreateContext(d,config,EGL_NO_CONTEXT,a);EGLSurface s=eglCreatePbufferSurface(d,config,pa);
    check(c!=EGL_NO_CONTEXT&&s!=EGL_NO_SURFACE&&eglMakeCurrent(d,s,s,c),"EGL context");
    GLuint shader=glCreateShader(GL_COMPUTE_SHADER);const char *text=source;glShaderSource(shader,1,&text,NULL);glCompileShader(shader);GLint ok;
    glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);if(!ok){char log[2048];glGetShaderInfoLog(shader,sizeof log,NULL,log);fprintf(stderr,"%s\n",log);}check(ok,"compute compile");
    GLuint program=glCreateProgram();glAttachShader(program,shader);glLinkProgram(program);glGetProgramiv(program,GL_LINK_STATUS,&ok);check(ok,"compute link");
    GLuint buffer;glGenBuffers(1,&buffer);glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);glBufferData(GL_SHADER_STORAGE_BUFFER,64*sizeof(uint32_t),NULL,GL_DYNAMIC_COPY);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buffer);
    glUseProgram(program);glDispatchCompute(1,1,1);glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    const uint32_t *result=glMapBufferRange(GL_SHADER_STORAGE_BUFFER,0,64*sizeof(uint32_t),GL_MAP_READ_BIT);check(result!=NULL,"result mapping");
    for(unsigned i=0;i<32;i++){check(result[i]==0x7f800001u+(i^17u),"cross-lane payload");if(result[32+i]!=528)fprintf(stderr,"REDUCTION lane=%u got=%u expected=528\n",i,result[32+i]);check(result[32+i]==528,"32-lane sum");}
    check(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER)&&glGetError()==GL_NO_ERROR,"result unmap");
    printf("{\"renderer\":\"%s\",\"cross_lane_payloads\":32,\"reductions\":32,\"all_passed\":true}\n",glGetString(GL_RENDERER));
    glDeleteBuffers(1,&buffer);glDeleteProgram(program);glDeleteShader(shader);free(source);eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(d,s);eglDestroyContext(d,c);eglTerminate(d);return 0;
}
