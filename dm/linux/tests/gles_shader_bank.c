// INPUT: GLSL experiment bank and an explicit newline-separated shader key list.
// OUTPUT: Real GLES compiler/linker results; no guest execution or FPS claims.
// POS: Host/target shader-bank validation using a surfaceless EGL pbuffer.
#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static void fail(const char *s){fprintf(stderr,"GLES_VALIDATION_STOP %s egl=%#x gl=%#x\n",s,eglGetError(),glGetError());exit(2);}
int main(int argc,char **argv){
    if(argc!=3&&argc!=4){fprintf(stderr,"usage: gles-shader-bank BANK KEY_LIST [GRAPHICS_PAIRS]\n");return 2;}
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);if(display==EGL_NO_DISPLAY||!eglInitialize(display,NULL,NULL)||!eglBindAPI(EGL_OPENGL_ES_API))fail("EGL initialize");
    const EGLint attributes[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};EGLConfig config;EGLint count;
    if(!eglChooseConfig(display,attributes,&config,1,&count)||count!=1)fail("EGL config");
    const EGLint context_attributes[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,2,EGL_NONE};
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,context_attributes);if(context==EGL_NO_CONTEXT)fail("GLES 3.2 context");
    const EGLint surface_attributes[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};EGLSurface surface=eglCreatePbufferSurface(display,config,surface_attributes);
    if(surface==EGL_NO_SURFACE||!eglMakeCurrent(display,surface,surface,context))fail("EGL current");
    fprintf(stderr,"GLES_VALIDATION_RENDERER %s / %s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    static struct {char key[17];GLuint object;uint32_t stage;} compiled[8192];unsigned ncompiled=0;
    FILE *keys=fopen(argv[2],"r");if(!keys)fail("key list");char key[128];unsigned passed=0,rejected=0,missing=0,paired=0,pair_rejected=0;
    while(fgets(key,sizeof key,keys)){
        key[strcspn(key,"\r\n")]=0;if(strlen(key)!=16||strspn(key,"0123456789abcdef")!=16)fail("key syntax");
        char path[2048];snprintf(path,sizeof path,"%s/%s.glmeta",argv[1],key);FILE *f=fopen(path,"rb");if(!f){missing++;continue;}uint32_t h[12];if(fread(h,sizeof h,1,f)!=1)fail("metadata read");fclose(f);
        if(h[0]!=0x4d4c4744||(h[1]!=7&&h[1]!=8)||(h[2]!=0&&h[2]!=1&&h[2]!=5))fail("metadata schema 7 or 8");
        snprintf(path,sizeof path,"%s/%s.glsl",argv[1],key);f=fopen(path,"rb");if(!f)fail("GLSL read");if(fseek(f,0,SEEK_END))fail("GLSL seek");long bytes=ftell(f);if(bytes<0||bytes>16*1024*1024||fseek(f,0,SEEK_SET))fail("GLSL length");
        char *text=malloc((size_t)bytes+1);if(!text||fread(text,1,bytes,f)!=(size_t)bytes)fail("GLSL allocation/read");text[bytes]=0;fclose(f);
        GLuint shader=glCreateShader(h[2]==0?GL_VERTEX_SHADER:h[2]==1?GL_FRAGMENT_SHADER:GL_COMPUTE_SHADER);const char *source=text;glShaderSource(shader,1,&source,NULL);glCompileShader(shader);GLint ok;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);GLuint program=0;
        if(ok){program=glCreateProgram();if(h[2]!=5)glProgramParameteri(program,GL_PROGRAM_SEPARABLE,GL_TRUE);glAttachShader(program,shader);glLinkProgram(program);glGetProgramiv(program,GL_LINK_STATUS,&ok);}
        if(ok){passed++;if(ncompiled==8192)fail("key count");memcpy(compiled[ncompiled].key,key,17);compiled[ncompiled].object=shader;compiled[ncompiled++].stage=h[2];}
        else {char log[4096]={0};if(program)glGetProgramInfoLog(program,sizeof log,NULL,log);else glGetShaderInfoLog(shader,sizeof log,NULL,log);fprintf(stderr,"GLES_VALIDATION_REJECT key=%s stage=%u log=%s\n",key,h[2],log);rejected++;glDeleteShader(shader);}
        if(program)glDeleteProgram(program);
        free(text);
    }
    fclose(keys);
    if(argc==4){FILE *pairs=fopen(argv[3],"r");if(!pairs)fail("graphics pairs");char vs[17],fs[17];
        while(fscanf(pairs,"%16s %16s",vs,fs)==2){GLuint v=0,f=0;for(unsigned i=0;i<ncompiled;i++){if(!strcmp(compiled[i].key,vs)&&compiled[i].stage==0)v=compiled[i].object;if(!strcmp(compiled[i].key,fs)&&compiled[i].stage==1)f=compiled[i].object;}if(!v||!f)fail("pair stage missing");
            GLuint program=glCreateProgram();glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);
            if(ok)paired++;else {char log[4096]={0};glGetProgramInfoLog(program,sizeof log,NULL,log);fprintf(stderr,"GLES_PAIR_REJECT vs=%s fs=%s log=%s\n",vs,fs,log);pair_rejected++;}glDeleteProgram(program);}
        fclose(pairs);}
    for(unsigned i=0;i<ncompiled;i++)glDeleteShader(compiled[i].object);
    printf("{\"compiled_and_linked\":%u,\"driver_rejected\":%u,\"offline_unsupported\":%u,\"paired_graphics_linked\":%u,\"paired_graphics_rejected\":%u,\"game_executed\":false,\"hardware_performance_verified\":false}\n",passed,rejected,missing,paired,pair_rejected);
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);return rejected||pair_rejected?1:0;
}
