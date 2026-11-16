// INPUT: Sampleable ASTC 2D source and a complete decoded destination framebuffer.
// OUTPUT: GPU-decoded texels at exact mip/region coordinates, retaining source compression.
// POS: GLES copy module for prepared ASTC assets and original BC material arrays.
#ifndef NVN_GL_ASTC_COPY_H
#define NVN_GL_ASTC_COPY_H
#include <GLES3/gl32.h>
#include <stdio.h>
typedef struct {GLuint program;GLint source,level,source_origin,destination_origin;} NvnGlAstcCopy;
static inline GLenum nvn_gl_astc_copy_init(NvnGlAstcCopy *copy){
    if(copy->program)return GL_NO_ERROR;
    const char *sources[]={
        "#version 320 es\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0.0,1.0);}",
        "#version 320 es\nprecision highp float;precision highp int;uniform highp sampler2D sourceImage;uniform int sourceLevel;uniform ivec2 sourceOrigin,destinationOrigin;layout(location=0) out vec4 result;void main(){ivec2 p=ivec2(gl_FragCoord.xy)-destinationOrigin+sourceOrigin;result=texelFetch(sourceImage,p,sourceLevel);}"};
    GLuint shaders[2]={0},program=glCreateProgram();GLint ok=1;
    for(unsigned i=0;i<2&&ok;i++){
        shaders[i]=glCreateShader(i?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);glShaderSource(shaders[i],1,&sources[i],NULL);glCompileShader(shaders[i]);glGetShaderiv(shaders[i],GL_COMPILE_STATUS,&ok);
        if(ok)glAttachShader(program,shaders[i]);
        else{char log[2048]={0};glGetShaderInfoLog(shaders[i],sizeof log,NULL,log);fprintf(stderr,"GLES_ASTC_COPY_COMPILE %s\n",log);}
    }
    if(ok){glLinkProgram(program);glGetProgramiv(program,GL_LINK_STATUS,&ok);}
    for(unsigned i=0;i<2;i++)if(shaders[i])glDeleteShader(shaders[i]);
    if(!ok){glDeleteProgram(program);return GL_INVALID_OPERATION;}
    copy->program=program;copy->source=glGetUniformLocation(program,"sourceImage");copy->level=glGetUniformLocation(program,"sourceLevel");
    copy->source_origin=glGetUniformLocation(program,"sourceOrigin");copy->destination_origin=glGetUniformLocation(program,"destinationOrigin");return glGetError();
}
/* Caller validates source/destination extents, chooses the destination layer
 * and supplies a spare sampler unit. Only color conversion is performed; NVN
 * draw state is restored by the next draw's normal state application. */
static inline GLenum nvn_gl_astc_copy_draw(NvnGlAstcCopy *copy,GLuint source,unsigned unit,
        int level,int sx,int sy,int dx,int dy,int width,int height){
    if(!source||level<0||sx<0||sy<0||dx<0||dy<0||width<1||height<1)return GL_INVALID_VALUE;
    GLenum error=nvn_gl_astc_copy_init(copy);if(error)return error;
    glBindProgramPipeline(0);glUseProgram(copy->program);
    glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,source);glBindSampler(unit,0);
    glUniform1i(copy->source,unit);glUniform1i(copy->level,level);glUniform2i(copy->source_origin,sx,sy);glUniform2i(copy->destination_origin,dx,dy);
    glViewport(dx,dy,width,height);glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_CULL_FACE);glDisablei(GL_BLEND,0);glColorMaski(0,GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glDrawArrays(GL_TRIANGLES,0,3);glUseProgram(0);glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT|GL_FRAMEBUFFER_BARRIER_BIT);return glGetError();
}
#endif
