// INPUT: Actual GLES buffer format, scalar type and texel size.
// OUTPUT: The typed word decoder layout shared with prepared GLSL.
// POS: Format contract for direct read-only buffer samplers.
#ifndef NVN_GL_BUFFER_SAMPLE_H
#define NVN_GL_BUFFER_SAMPLE_H
#include <GLES3/gl32.h>
static inline int nvn_gl_sampler_layout(GLenum format,GLenum type,unsigned bpp,int scalar,GLint out[3]){
    int integer=format==GL_RED_INTEGER||format==GL_RG_INTEGER||format==GL_RGB_INTEGER||format==GL_RGBA_INTEGER;
    if((scalar!=0)!=integer)return 0;
    unsigned bytes;int encoding;
    switch(type){
    case GL_UNSIGNED_BYTE:bytes=1;encoding=integer?1:0;if(scalar==1)return 0;break;
    case GL_HALF_FLOAT:bytes=2;encoding=2;break;
    case GL_FLOAT:bytes=4;encoding=3;break;
    case GL_UNSIGNED_INT:bytes=4;encoding=4;if(scalar!=2)return 0;break;
    case GL_INT:bytes=4;encoding=5;if(scalar!=1)return 0;break;
    default:return 0;
    }
    if(!bpp||bpp%bytes||bpp/bytes>4)return 0;
    out[0]=bpp;out[1]=bpp/bytes;out[2]=encoding;return 1;
}
#endif
