// INPUT: NVN texture/sampler snapshots and Tegra block-linear guest bytes.
// OUTPUT: GLES immutable storage, decoded uploads and explicit view exclusions.
// POS: Texture half of the optional GLES verification backend.
static int gl_format(unsigned f,GLenum *internal,GLenum *format,GLenum *type,unsigned *bpp){
    if(f==NVN_FORMAT_R16){*internal=L.norm16?GL_R16_EXT:GL_R32F;*format=GL_RED;*type=L.norm16?GL_UNSIGNED_SHORT:GL_FLOAT;*bpp=L.norm16?2:4;return 1;}
#define F(n,i,e,t,b) case NVN_FORMAT_##n:*internal=GL_##i;*format=GL_##e;*type=GL_##t;*bpp=b;return 1
    switch(f){
    F(R8,R8,RED,UNSIGNED_BYTE,1);F(RG8,RG8,RG,UNSIGNED_BYTE,2);F(RGBA8,RGBA8,RGBA,UNSIGNED_BYTE,4);
    F(RGBA8_SRGB,SRGB8_ALPHA8,RGBA,UNSIGNED_BYTE,4);
    F(R8SN,R8_SNORM,RED,BYTE,1);F(RG8SN,RG8_SNORM,RG,BYTE,2);F(RGBA8SN,RGBA8_SNORM,RGBA,BYTE,4);
    F(R8UI,R8UI,RED_INTEGER,UNSIGNED_BYTE,1);F(RG8UI,RG8UI,RG_INTEGER,UNSIGNED_BYTE,2);F(RGBA8UI,RGBA8UI,RGBA_INTEGER,UNSIGNED_BYTE,4);
    F(R16F,R16F,RED,HALF_FLOAT,2);F(RG16F,RG16F,RG,HALF_FLOAT,4);F(RGBA16F,RGBA16F,RGBA,HALF_FLOAT,8);
    F(R32F,R32F,RED,FLOAT,4);F(RG32F,RG32F,RG,FLOAT,8);F(RGB32F,RGB32F,RGB,FLOAT,12);F(RGBA32F,RGBA32F,RGBA,FLOAT,16);
    F(R32UI,R32UI,RED_INTEGER,UNSIGNED_INT,4);F(RG32UI,RG32UI,RG_INTEGER,UNSIGNED_INT,8);F(RGBA32UI,RGBA32UI,RGBA_INTEGER,UNSIGNED_INT,16);
    F(R32I,R32I,RED_INTEGER,INT,4);F(RG32I,RG32I,RG_INTEGER,INT,8);F(RGBA32I,RGBA32I,RGBA_INTEGER,INT,16);
    F(RGB565,RGB565,RGB,UNSIGNED_SHORT_5_6_5,2);F(RGB10A2,RGB10_A2,RGBA,UNSIGNED_INT_2_10_10_10_REV,4);
    F(R11G11B10F,R11F_G11F_B10F,RGB,UNSIGNED_INT_10F_11F_11F_REV,4);
    F(DEPTH16,DEPTH_COMPONENT16,DEPTH_COMPONENT,UNSIGNED_SHORT,2);F(DEPTH24_STENCIL8,DEPTH24_STENCIL8,DEPTH_STENCIL,UNSIGNED_INT_24_8,4);
    F(DEPTH32F,DEPTH_COMPONENT32F,DEPTH_COMPONENT,FLOAT,4);F(DEPTH32F_STENCIL8,DEPTH32F_STENCIL8,DEPTH_STENCIL,FLOAT_32_UNSIGNED_INT_24_8_REV,8);
    default:return 0;
    }
#undef F
}
static int gl_is_depth(const GlTexture *t){return t->format==GL_DEPTH_COMPONENT||t->format==GL_DEPTH_STENCIL;}
static int gl_buffer_format(GLenum f){
    /* GLES 3.2 table 8.18 excludes normalized signed, sRGB and packed formats. */
    switch(f){case GL_R8:case GL_R16F:case GL_R32F:case GL_R8UI:case GL_R32UI:case GL_R32I:
    case GL_RG8:case GL_RG16F:case GL_RG32F:case GL_RG8UI:case GL_RG32UI:case GL_RG32I:
    case GL_RGB32F:case GL_RGB32UI:case GL_RGB32I:case GL_RGBA8:case GL_RGBA16F:case GL_RGBA32F:case GL_RGBA8UI:case GL_RGBA32UI:case GL_RGBA32I:return 1;default:return 0;}
}
static unsigned gl_dim(unsigned n,unsigned level){n=level<32?n>>level:0;return n?n:1;}
static int gl_storage_3d(GLenum target){return target==GL_TEXTURE_3D||target==GL_TEXTURE_2D_ARRAY||target==GL_TEXTURE_CUBE_MAP_ARRAY;}
#include "nvn_gl_astc_stream.inc"
static GlTexture *gl_texture(const tex_t *source,int upload){
    if(!source||!source->serial)return NULL;
    /* Resource creation/uploads must not replace an earlier shader binding. */
    glActiveTexture(GL_TEXTURE0+L.max_tex-1);
    GlTexture *t=NULL;for(unsigned i=0;i<L.ntexture;i++)if(L.textures[i].source.serial==source->serial){t=&L.textures[i];break;}
    if(!t){
        if(L.ntexture==GL_OBJECTS)gl_stop("texture count",L.ntexture);t=&L.textures[L.ntexture++];memset(t,0,sizeof *t);t->source=*source;t->version=1;
        if(source->d.samples>1){t->unsupported=1;gl_filter("multisample_texture",source->d.samples);return NULL;}
        switch(source->d.target){
        case NVN_TEXTURE_TARGET_2D:t->target=GL_TEXTURE_2D;break;
        case NVN_TEXTURE_TARGET_2D_ARRAY:t->target=GL_TEXTURE_2D_ARRAY;break;
        case NVN_TEXTURE_TARGET_3D:t->target=GL_TEXTURE_3D;break;
        case NVN_TEXTURE_TARGET_CUBEMAP:t->target=GL_TEXTURE_CUBE_MAP;break;
        case NVN_TEXTURE_TARGET_CUBEMAP_ARRAY:t->target=GL_TEXTURE_CUBE_MAP_ARRAY;break;
        case NVN_TEXTURE_TARGET_1D:t->target=GL_TEXTURE_2D;break;
        case NVN_TEXTURE_TARGET_BUFFER:t->target=GL_TEXTURE_BUFFER;break;
        default:t->unsupported=1;gl_filter("texture_target",source->d.target);return NULL;
        }
        uint32_t decoded;int decoded_bpp;
        if(nvn_bc_unpacked(source->d.format,&decoded,&decoded_bpp)){
            t->bpp=decoded_bpp;t->type=decoded==115?GL_HALF_FLOAT:decoded==12||decoded==32?GL_BYTE:GL_UNSIGNED_BYTE;
            t->format=decoded==10||decoded==12?GL_RED:decoded==30||decoded==32?GL_RG:GL_RGBA;
            t->internal=decoded==10?GL_R8:decoded==12?GL_R8_SNORM:decoded==30?GL_RG8:decoded==32?GL_RG8_SNORM:decoded==71?GL_SRGB8_ALPHA8:decoded==115?GL_RGBA16F:GL_RGBA8;
        }else if(source->d.format>=NVN_FORMAT_RGBA_ASTC_4x4&&source->d.format<=NVN_FORMAT_RGBA_ASTC_12x12_SRGB){
            unsigned index=source->d.format-NVN_FORMAT_RGBA_ASTC_4x4;t->internal=(index<14?0x93b0:0x93d0)+index%14;t->bpp=0;
        }else if(!gl_format(source->d.format,&t->internal,&t->format,&t->type,&t->bpp)){
            t->unsupported=1;gl_filter("texture_format",source->d.format);return NULL;
        }
        t->unorm16_float=source->d.format==NVN_FORMAT_R16&&!L.norm16;
        /* R32F retains all 16 normalized input bits where EXT_texture_norm16 is
         * absent. This supports sampling/uploads, not R16 render quantization. */
        if(t->unorm16_float)fprintf(stderr,"[nvn-gles] R16_SAMPLED_FLOAT serial=%llu precision_bits=16\n",(unsigned long long)source->serial);
        glGenTextures(1,&t->name);glBindTexture(t->target,t->name);unsigned h=source->d.target==NVN_TEXTURE_TARGET_1D?1:source->d.height;
        if(t->target==GL_TEXTURE_BUFFER)goto texture_ready;
        if(gl_storage_3d(t->target))glTexStorage3D(t->target,source->d.levels,t->internal,source->d.width,h,t->target==GL_TEXTURE_3D?source->d.depth:source->L.layers);
        else glTexStorage2D(t->target,source->d.levels,t->internal,source->d.width,h);
        GLenum error=glGetError();if(error){t->unsupported=1;glDeleteTextures(1,&t->name);gl_filter("texture_storage",error);return NULL;}
        glTexParameteri(t->target,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(t->target,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(t->target,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(t->target,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
texture_ready:
    if(t->unsupported)return NULL;
    /* Compressed destinations need a CPU baseline before their first GPU copy. */
    if(!t->bpp&&!t->stamp&&source->mem)upload=1;
    if(t->target==GL_TEXTURE_BUFFER){uint64_t offset;
        if(!gl_buffer_format(t->internal)){t->unsupported=1;gl_filter("texture_buffer_format",t->internal);return NULL;}
        if(!t->bpp||source->L.size%t->bpp||source->L.size/t->bpp>(uint64_t)L.max_texbuf){t->unsupported=1;gl_filter("texture_buffer_size_limit",source->L.size);return NULL;}
        GlPool *p=gl_upload((GlRange){(uint64_t)(uintptr_t)source->mem,source->L.size},&offset);
        if(offset%L.texbuf_align){t->unsupported=1;gl_filter("texture_buffer_offset_alignment",offset);return NULL;}
        glBindTexture(t->target,t->name);glTexBufferRange(t->target,t->internal,p->backing.name,offset,source->L.size);return t;}
    if(!upload||!source->mem)return t;
    if(t->gpu_owned){if(t->cpu_snapshot&&nvn_dirty_since(source->mem,source->L.size,t->stamp))gl_refresh_astc_cpu(t,source);return t;}
    if(!gl_pool((uint64_t)(uintptr_t)source->mem,source->L.size,NULL))gl_stop("texture source outside live pool",source->serial);
    if(t->stamp&&!nvn_dirty_since(source->mem,source->L.size,t->stamp))return t;
    uint64_t stamp=nvn_dirty_now();nvn_dirty_protect(source->mem,source->L.size);
    uint8_t *snapshot=NULL;if(!t->bpp){snapshot=malloc(source->L.size);if(!snapshot)gl_stop("ASTC CPU baseline allocation",source->L.size);memcpy(snapshot,source->mem,source->L.size);}
    glBindTexture(t->target,t->name);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    for(unsigned layer=0;layer<source->L.layers;layer++)for(unsigned level=0;level<source->d.levels;level++){
        uint32_t rb,rows,slices,block;uint64_t base;unsigned th,td;
        if(nvn_deswizzle_geom(&source->d,level,layer,&rb,&rows,&slices,&base,&th,&td,&block))gl_stop("texture upload layout",source->serial);
        size_t bytes=(size_t)rb*rows*slices;uint8_t *linear=malloc(bytes);if(!linear)gl_stop("texture upload allocation",bytes);
        if(nvn_deswizzle(&source->d,level,layer,snapshot?snapshot:source->mem,linear,rb))gl_stop("texture deswizzle",source->serial);
        unsigned w=gl_dim(source->d.width,level),h=source->d.target==NVN_TEXTURE_TARGET_1D?1:gl_dim(source->d.height,level);
        uint8_t *pixels=linear;uint32_t unpack;int bpp;
        if(nvn_bc_unpacked(source->d.format,&unpack,&bpp)){
            bytes=(size_t)w*h*slices*bpp;pixels=malloc(bytes);if(!pixels)gl_stop("texture decode allocation",bytes);
            for(unsigned z=0;z<slices;z++)if(nvn_bc_decode(source->d.format,linear+(size_t)rb*rows*z,rb,w,h,pixels+(size_t)w*h*bpp*z,(size_t)w*bpp))gl_stop("BC decode",source->d.format);
        }else if(t->unorm16_float){
            bytes=(size_t)w*h*slices*sizeof(float);pixels=malloc(bytes);if(!pixels)gl_stop("norm16 decode allocation",bytes);
            for(unsigned z=0;z<slices;z++)for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){uint16_t value;memcpy(&value,linear+(size_t)rb*(rows*z+y)+x*2,2);((float *)pixels)[((size_t)z*h+y)*w+x]=(float)value/65535.0f;}
        }
        GLenum target=t->target==GL_TEXTURE_CUBE_MAP?GL_TEXTURE_CUBE_MAP_POSITIVE_X+layer:t->target;
        if(!t->bpp){
            if(gl_storage_3d(t->target))glCompressedTexSubImage3D(target,level,0,0,t->target==GL_TEXTURE_3D?0:layer,w,h,slices,t->internal,bytes,pixels);
            else glCompressedTexSubImage2D(target,level,0,0,w,h,t->internal,bytes,pixels);
        }else if(gl_storage_3d(t->target))glTexSubImage3D(target,level,0,0,t->target==GL_TEXTURE_3D?0:layer,w,h,slices,t->format,t->type,pixels);
        else glTexSubImage2D(target,level,0,0,w,h,t->format,t->type,pixels);
        if(pixels!=linear)free(pixels);free(linear);
        GLenum error=glGetError();if(error){free(snapshot);t->unsupported=1;gl_filter("texture_upload",error);return NULL;}
    }
    if(snapshot)gl_astc_baseline(t,snapshot);
    t->stamp=stamp;t->version++;L.uploads++;return t;
}
static int gl_view_supported(const view_t *v,const tex_t *source){
    if(!v)return 1;
    if((v->set_levels&&(v->base_level<0||v->nlevels<=0||(unsigned)v->base_level>=source->d.levels||(unsigned)v->nlevels>source->d.levels-v->base_level))||
       (v->set_layers&&(v->min_layer||v->nlayers!=(int)source->L.layers))||
       (v->set_format&&v->format!=(int)source->d.format)||
       (v->set_target&&v->target!=source->d.target)||(v->set_swizzle&&source->d.target==NVN_TEXTURE_TARGET_BUFFER)){
        static unsigned reported;
        if(reported++<16)fprintf(stderr,"[nvn-gles] VIEW_REJECT serial=%llu source=%ux%ux%u levels=%u layers=%u format=%u target=%u levels_set=%u base=%d count=%d layers_set=%u layer=%d count=%d format_set=%u format=%d target_set=%u target=%u swizzle_set=%u swizzle=%u,%u,%u,%u\n",
            (unsigned long long)source->serial,source->d.width,source->d.height,source->d.depth,source->d.levels,source->L.layers,source->d.format,source->d.target,
            v->set_levels,v->base_level,v->nlevels,v->set_layers,v->min_layer,v->nlayers,v->set_format,v->format,v->set_target,v->target,v->set_swizzle,v->swizzle[0],v->swizzle[1],v->swizzle[2],v->swizzle[3]);
        return gl_filter("sampled_texture_view",source->serial);
    }
    if(v->set_swizzle)for(unsigned i=0;i<4;i++)if(v->swizzle[i]>NVN_TEXTURE_SWIZZLE_A)gl_stop("sampled view swizzle",v->swizzle[i]);
    return 1;
}
static GLuint gl_sampled_view(GlTextureSlot *slot,GlTexture *t){
    const view_t *v=slot->has_view?&slot->view:NULL;
    if(!gl_view_supported(v,&slot->source))return 0;
    if(!v||(!v->set_swizzle&&(!v->set_levels||(!v->base_level&&v->nlevels==(int)t->source.d.levels))))return t->name;
    if(t->target!=GL_TEXTURE_2D&&!gl_storage_3d(t->target)&&t->target!=GL_TEXTURE_CUBE_MAP)return gl_filter("sampled_mip_view_target",t->target);
    unsigned base=v->set_levels?v->base_level:0,levels=v->set_levels?(unsigned)v->nlevels:t->source.d.levels,w=gl_dim(t->source.d.width,base),h=gl_dim(t->source.d.height,base);
    glActiveTexture(GL_TEXTURE0+L.max_tex-1);
    if(!slot->sampled_view){
        glGenTextures(1,&slot->sampled_view);glBindTexture(t->target,slot->sampled_view);
        if(gl_storage_3d(t->target))glTexStorage3D(t->target,levels,t->internal,w,h,t->target==GL_TEXTURE_3D?gl_dim(t->source.d.depth,base):t->source.L.layers);
        else glTexStorage2D(t->target,levels,t->internal,w,h);
        glTexParameteri(t->target,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(t->target,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(t->target,GL_TEXTURE_MAX_LEVEL,levels-1);
        if(v->set_swizzle){const GLenum components[]={GL_ZERO,GL_ONE,GL_RED,GL_GREEN,GL_BLUE,GL_ALPHA};for(unsigned i=0;i<4;i++)glTexParameteri(t->target,GL_TEXTURE_SWIZZLE_R+i,components[v->swizzle[i]]);}
        for(unsigned i=0;i<levels;i++)slot->sampled_view_bytes+=(uint64_t)gl_dim(w,i)*gl_dim(h,i)*t->bpp*(t->target==GL_TEXTURE_3D?gl_dim(t->source.d.depth,base+i):t->source.L.layers);
        L.sampled_view_bytes+=slot->sampled_view_bytes;
    }
    if(slot->sampled_version==t->version)return slot->sampled_view;
    /* Independent read snapshots also avoid sampling a mip of the render target. */
    for(unsigned i=0;i<levels;i++){unsigned depth=t->target==GL_TEXTURE_3D?gl_dim(t->source.d.depth,base+i):t->source.L.layers;glCopyImageSubData(t->name,t->target,base+i,0,0,0,slot->sampled_view,t->target,i,0,0,0,gl_dim(w,i),gl_dim(h,i),depth);L.view_copies++;}
    GLenum error=glGetError();if(error){gl_filter("sampled_mip_view_copy",error);return 0;}
    slot->sampled_version=t->version;return slot->sampled_view;
}
static GLuint gl_buffer_sampled_view(GlTextureSlot *slot,const GlBinding *b,unsigned *count){
    const tex_t *source=&slot->source;GLenum internal,format,type;unsigned bpp;
    if(source->d.target!=NVN_TEXTURE_TARGET_BUFFER||!gl_view_supported(slot->has_view?&slot->view:NULL,source))return gl_filter("buffer_sampler_view",source->serial);
    if((source->d.format==NVN_FORMAT_R16&&!L.norm16)||!gl_format(source->d.format,&internal,&format,&type,&bpp)||!gl_buffer_format(internal)||!bpp||source->L.size%bpp)return gl_filter("buffer_sampler_format",source->d.format);
    int integer=format==GL_RED_INTEGER||format==GL_RG_INTEGER||format==GL_RGB_INTEGER||format==GL_RGBA_INTEGER;
    if(b->flags<0||b->flags>2||(b->flags==0&&integer)||(b->flags!=0&&!integer)||(b->flags==1&&type!=GL_INT)||(b->flags==2&&type!=GL_UNSIGNED_INT&&type!=GL_UNSIGNED_BYTE))return gl_filter("buffer_sampler_numeric_type",source->d.format);
    uint64_t texels=source->L.size/bpp,height=(texels+2047)/2048;unsigned width=texels<2048?texels:2048;
    if(!texels||texels>INT32_MAX||height>(uint64_t)L.max_texture2d||width>(unsigned)L.max_texture2d)return gl_filter("buffer_sampler_size",texels);
    if(L.resource_reuse){*count=(unsigned)texels;return gl_resource_snapshot((GlRange){(uint64_t)(uintptr_t)source->mem,source->L.size},internal,format,type,bpp,1);}
    uint64_t offset;GlPool *pool=gl_upload((GlRange){(uint64_t)(uintptr_t)source->mem,source->L.size},&offset);
    glActiveTexture(GL_TEXTURE0+L.max_tex-1);
    if(!slot->sampled_view){
        glGenTextures(1,&slot->sampled_view);glBindTexture(GL_TEXTURE_2D,slot->sampled_view);glTexStorage2D(GL_TEXTURE_2D,1,internal,width,height);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
        slot->sampled_view_bytes=(uint64_t)width*height*bpp;L.sampled_view_bytes+=slot->sampled_view_bytes;
    }
    glBindTexture(GL_TEXTURE_2D,slot->sampled_view);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pool->backing.name);glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    unsigned rows=texels/width,tail=texels%width;
    if(rows)glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,rows,format,type,(void *)(uintptr_t)offset);
    if(tail)glTexSubImage2D(GL_TEXTURE_2D,0,0,rows,tail,1,format,type,(void *)(uintptr_t)(offset+(uint64_t)rows*width*bpp));
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);GLenum error=glGetError();if(error)return gl_filter("buffer_sampler_snapshot",error);
    L.snapshot_calls[1]++;L.snapshot_bytes[1]+=source->L.size;*count=texels;return slot->sampled_view;
}
static float gl_pixel_component(const uint8_t *p,GLenum type){
    if(type==GL_UNSIGNED_BYTE)return *p/255.0f;
    if(type==GL_FLOAT){float f;memcpy(&f,p,4);return f;}
    if(type==GL_UNSIGNED_INT){uint32_t u;memcpy(&u,p,4);return (float)u;}
    if(type==GL_INT){int32_t i;memcpy(&i,p,4);return (float)i;}
    uint16_t h;memcpy(&h,p,2);unsigned e=(h>>10)&31,m=h&1023;float f=e==31?(m?NAN:INFINITY):e?ldexpf(1.0f+m/1024.0f,(int)e-15):ldexpf((float)m,-24);return h&32768?-f:f;
}
static float gl_unsigned_float_component(uint32_t bits,unsigned mantissa){
    unsigned exponent=bits>>mantissa,value=bits&((1u<<mantissa)-1);
    return exponent==31?(value?NAN:INFINITY):exponent?ldexpf(1.0f+value/(float)(1u<<mantissa),(int)exponent-15):ldexpf((float)value,1-15-(int)mantissa);
}
static uint64_t gl_trace_pixels(GlTexture *t,const char *key,uint64_t draw){
    int diagnostic=L.diag_present==L.presents+1;
    if(!t||(!diagnostic&&(!L.trace_pixels||L.presents)))return 0;
    if(!diagnostic&&draw!=52&&draw!=55&&draw!=72&&draw!=89&&draw!=90)return 0;
    unsigned level=L.state.rt.has_cview[0]?L.state.rt.cview[0].base_level:0,w=gl_dim(t->source.d.width,level),h=gl_dim(t->source.d.height,level),x=0,y=0;
    if(diagnostic){if(w>8){x=w/2-4;w=8;}if(h>8){y=h/2-4;h=8;}}
    glBindFramebuffer(GL_READ_FRAMEBUFFER,L.fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
    GLint format=0,type=0;glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT,&format);glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE,&type);
    unsigned channels=format==GL_RED||format==GL_RED_INTEGER?1:format==GL_RG||format==GL_RG_INTEGER?2:format==GL_RGB||format==GL_RGB_INTEGER?3:format==GL_RGBA||format==GL_RGBA_INTEGER?4:0;
    int packed=type==GL_UNSIGNED_INT_10F_11F_11F_REV&&format==GL_RGB;
    unsigned bytes=type==GL_UNSIGNED_BYTE?1:type==GL_HALF_FLOAT?2:type==GL_FLOAT||type==GL_UNSIGNED_INT||type==GL_INT||packed?4:0;
    if(!channels||!bytes){fprintf(stderr,"[nvn-gles] PIXELS_UNAVAILABLE draw=%llu format=%u type=%u\n",(unsigned long long)draw,format,type);return 0;}
    size_t size=(size_t)w*h*(packed?4:channels*bytes);uint8_t *pixels=malloc(size);if(!pixels)gl_stop("pixel diagnostics allocation",size);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);glReadPixels(x,y,w,h,format,type,pixels);GLenum error=glGetError();
    uint64_t colored=0,nonfinite=0;float low=INFINITY,high=-INFINITY,alpha=0;
    if(!error)for(size_t i=0;i<(size_t)w*h;i++){int color=0;for(unsigned c=0;c<channels;c++){float f;
        if(packed){uint32_t bits;memcpy(&bits,pixels+i*4,4);f=gl_unsigned_float_component(c==0?bits&2047:c==1?(bits>>11)&2047:bits>>22,c==2?5:6);}
        else f=gl_pixel_component(pixels+(i*channels+c)*bytes,type);
        if(c==3){if(f>alpha)alpha=f;continue;}if(!isfinite(f)){nonfinite++;continue;}if(f<low)low=f;if(f>high)high=f;color|=fabsf(f)>1e-7f;}colored+=color;}
    fprintf(stderr,"[nvn-gles] TARGET_PIXELS draw=%llu key=%s serial=%llu size=%ux%u read_format=%u read_type=%u colored=%llu nonfinite=%llu min=%g max=%g alpha_max=%g error=%u\n",
        (unsigned long long)draw,key,(unsigned long long)t->source.serial,w,h,format,type,(unsigned long long)colored,(unsigned long long)nonfinite,low,high,alpha,error);free(pixels);return nonfinite;
}
static void gl_memory_profile(void){
    fprintf(stderr,"[nvn-gles] ASTC_STREAM_MEMORY baseline_bytes=%llu refreshes=%llu refreshed_blocks=%llu\n",(unsigned long long)L.astc_baseline_bytes,(unsigned long long)L.astc_refreshes,(unsigned long long)L.astc_refresh_blocks);
    uint64_t capacity=0,backed=0,dense=0,grows=0,copied=0,scratch=0;unsigned buffers=0;
    for(unsigned i=0;i<L.npool;i++){
        GlPool *p=&L.pools[i];capacity+=p->size;if(!p->backing.name)continue;
        backed+=p->backing.capacity;dense+=p->size;buffers++;grows+=p->backing.grows;copied+=p->backing.copied_bytes;
        if(p->backing.peak_scratch>scratch)scratch=p->backing.peak_scratch;
        fprintf(stderr,"[nvn-gles] POOL_BACKING serial=%llu pool_bytes=%llu used_end=%llu backing_bytes=%llu grows=%llu\n",
            (unsigned long long)p->serial,(unsigned long long)p->size,(unsigned long long)p->used_end,(unsigned long long)p->backing.capacity,(unsigned long long)p->backing.grows);
    }
    fprintf(stderr,"[nvn-gles] GLES_MEMORY pool_capacity_bytes=%llu buffer_backing_capacity_bytes=%llu dense_equivalent_bytes=%llu buffer_objects=%u backing_grows=%llu growth_copied_bytes=%llu growth_peak_scratch_bytes=%llu shader_index_bytes=%zu shader_index_file_backed=1 shader_index_table_bytes=%zu sampled_view_bytes=%llu textures=%u physical_residency_not_measured=1\n",
        (unsigned long long)capacity,(unsigned long long)backed,(unsigned long long)dense,buffers,(unsigned long long)grows,(unsigned long long)copied,(unsigned long long)scratch,L.index_size,L.nindex*sizeof *L.index,(unsigned long long)L.sampled_view_bytes,L.ntexture);
}
static void gl_attach(GLenum fbo,GLenum attachment,GlTexture *t,const view_t *view){
    L.target_valid=0;
    int level=view&&view->set_levels?view->base_level:0,layer=view&&view->set_layers?view->min_layer:0;
    if(!t){glFramebufferTexture2D(fbo,attachment,GL_TEXTURE_2D,0,0);return;}
    unsigned layers=t->target==GL_TEXTURE_3D?gl_dim(t->source.d.depth,level):t->source.L.layers;
    if(level<0||(unsigned)level>=t->source.d.levels||layer<0||(unsigned)layer>=layers)gl_stop("attachment view range",t->source.serial);
    if(gl_storage_3d(t->target))glFramebufferTextureLayer(fbo,attachment,t->name,level,layer);
    else glFramebufferTexture2D(fbo,attachment,t->target==GL_TEXTURE_CUBE_MAP?GL_TEXTURE_CUBE_MAP_POSITIVE_X+layer:t->target,t->name,level);
}
static int gl_target(void){
    gl_queries_end();glBindFramebuffer(GL_FRAMEBUFFER,L.fbo);
    /* Attachment changes terminate tile work on mobile drivers. A repeated
     * immutable NVN target needs no detach/reattach or framebuffer recheck.
     * Every attachment helper and texture release invalidates this snapshot. */
    if(L.target_valid&&!memcmp(&L.cached_target,&L.state.rt,sizeof L.cached_target)){L.target_hits++;return 1;}
    L.target_misses++;GLenum buffers[8];int valid=1;
    if(L.state.rt.n<0||L.state.rt.n>8)gl_stop("render target count",L.state.rt.n);
    if(L.state.rt.n>L.max_color||L.state.rt.n>L.max_draw)return gl_filter("render_target_binding_limit",L.state.rt.n);
    for(int i=0;i<8&&i<L.max_color;i++){GlTexture *t=i<L.state.rt.n?gl_texture(L.state.rt.color[i],0):NULL;
        if(i<L.state.rt.n&&L.state.rt.color[i]&&!t)valid=0;
        if(t&&t->unorm16_float)gl_stop("R16 render target requires native norm16",t->source.serial);
        gl_attach(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0+i,t,L.state.rt.has_cview[i]?&L.state.rt.cview[i]:NULL);buffers[i]=t?GL_COLOR_ATTACHMENT0+i:GL_NONE;}
    GlTexture *d=gl_texture(L.state.rt.depth,0);if(L.state.rt.depth&&!d)valid=0;
    gl_attach(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,NULL,NULL);gl_attach(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,NULL,NULL);
    if(d)gl_attach(GL_FRAMEBUFFER,d->format==GL_DEPTH_STENCIL?GL_DEPTH_STENCIL_ATTACHMENT:GL_DEPTH_ATTACHMENT,d,L.state.rt.has_dview?&L.state.rt.dview:NULL);
    glDrawBuffers(L.state.rt.n,buffers);L.passes++;
    if(!valid)return 0;GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if(status!=GL_FRAMEBUFFER_COMPLETE)return gl_filter("framebuffer_incomplete",status);
    L.cached_target=L.state.rt;L.target_valid=1;return 1;
}
void mtl_tex_release(tex_t *source){mtl_init();gl_enter();L.target_valid=0;glFinish();for(unsigned i=0;i<L.ntexture;i++)if(L.textures[i].source.serial==source->serial){glDeleteTextures(1,&L.textures[i].name);gl_astc_baseline(&L.textures[i],NULL);L.textures[i]=L.textures[--L.ntexture];break;}for(unsigned i=0;i<L.nslot;i++)if(L.slots[i].live&&L.slots[i].source.serial==source->serial){glDeleteTextures(1,&L.slots[i].sampled_view);L.sampled_view_bytes-=L.slots[i].sampled_view_bytes;L.slots[i].sampled_view=L.slots[i].sampled_view_bytes=L.slots[i].live=0;}gl_leave();}
void mtl_set_tex_pool_n(int n){mtl_init();gl_enter();if(n<0||n>1<<20)gl_stop("descriptor slots",n);if((unsigned)n>L.nslot){GlTextureSlot *p=realloc(L.slots,(size_t)n*sizeof *p);if(!p)gl_stop("descriptor allocation",n);memset(p+L.nslot,0,((unsigned)n-L.nslot)*sizeof *p);L.slots=p;L.nslot=n;}gl_leave();}
void mtl_reg_tex(int id,const tex_t *source,const view_t *view){
    mtl_init();gl_enter();if(id<0||(unsigned)id>=L.nslot)gl_stop("texture descriptor id",id);
    view_t effective;int has_view=nvn_texture_sampling_view(source,view,&effective);
    GlTextureSlot *s=&L.slots[id];if(s->live&&source&&s->source.serial==source->serial&&s->has_view==has_view&&(!has_view||!memcmp(&s->view,&effective,sizeof effective))){gl_leave();return;}
    glDeleteTextures(1,&s->sampled_view);L.sampled_view_bytes-=s->sampled_view_bytes;memset(s,0,sizeof *s);s->live=source!=NULL;
    if(source)s->source=*source;if(has_view){s->view=effective;s->has_view=1;}gl_leave();
}
/* CLAMP_TO_BORDER is core in the ES 3.2 context required by gl_initialize.
 * Preserve the guest border color; substituting edge texels changes D2R
 * sampling even when filtering the unsupported work would seem faster.
 * ES 3.2 sections 8.9 and 8.14: https://registry.khronos.org/OpenGL/specs/es/3.2/es_spec_3.2.pdf */
static GLint gl_wrap(unsigned mode){switch(mode){case NVN_WRAP_MODE_REPEAT:return GL_REPEAT;case NVN_WRAP_MODE_MIRRORED_REPEAT:return GL_MIRRORED_REPEAT;case NVN_WRAP_MODE_CLAMP_TO_EDGE:return GL_CLAMP_TO_EDGE;case NVN_WRAP_MODE_CLAMP_TO_BORDER:return GL_CLAMP_TO_BORDER;default:gl_filter("sampler_wrap_clamped",mode);return GL_CLAMP_TO_EDGE;}}
static int gl_sampler_border_supported(unsigned id,GLenum format){
    return !L.sampler_border[id]||(format!=GL_RED_INTEGER&&format!=GL_RG_INTEGER&&format!=GL_RGB_INTEGER&&format!=GL_RGBA_INTEGER);
}
void mtl_reg_samp(int id,const samp_par *p){
    mtl_init();gl_enter();if(id<0||id>=GL_OBJECTS)gl_stop("sampler descriptor",id);GLuint *s=&L.samplers[id];if(*s)glDeleteSamplers(1,s);glGenSamplers(1,s);
    const GLenum min[]={GL_NEAREST,GL_LINEAR,GL_NEAREST_MIPMAP_NEAREST,GL_LINEAR_MIPMAP_NEAREST,GL_NEAREST_MIPMAP_LINEAR,GL_LINEAR_MIPMAP_LINEAR};
    if(p->minf>5||p->magf>1)gl_stop("sampler filter",p->minf);glSamplerParameteri(*s,GL_TEXTURE_MIN_FILTER,min[p->minf]);glSamplerParameteri(*s,GL_TEXTURE_MAG_FILTER,p->magf?GL_LINEAR:GL_NEAREST);
    glSamplerParameteri(*s,GL_TEXTURE_WRAP_S,gl_wrap(p->ws));glSamplerParameteri(*s,GL_TEXTURE_WRAP_T,gl_wrap(p->wt));glSamplerParameteri(*s,GL_TEXTURE_WRAP_R,gl_wrap(p->wr));
    L.sampler_border[id]=p->ws==NVN_WRAP_MODE_CLAMP_TO_BORDER||p->wt==NVN_WRAP_MODE_CLAMP_TO_BORDER||p->wr==NVN_WRAP_MODE_CLAMP_TO_BORDER;
    if(L.sampler_border[id])glSamplerParameterfv(*s,GL_TEXTURE_BORDER_COLOR,p->border);
    glSamplerParameterf(*s,GL_TEXTURE_MIN_LOD,p->lod_min);glSamplerParameterf(*s,GL_TEXTURE_MAX_LOD,p->lod_max);
    glSamplerParameteri(*s,GL_TEXTURE_COMPARE_MODE,p->cmp_mode?GL_COMPARE_REF_TO_TEXTURE:GL_NONE);glSamplerParameteri(*s,GL_TEXTURE_COMPARE_FUNC,GL_NEVER+p->cmp_func-1);gl_check("sampler");gl_leave();
}
static void gl_capture(unsigned width,unsigned height){
    const char *root=getenv("DM_FRAME_DIR");if(!root)return;char request[1400];snprintf(request,sizeof request,"%s/capture-request",root);
    if(access(request,F_OK))return;size_t row=(size_t)width*4,bytes=row*height;uint8_t *pixels=malloc(bytes),*rgb=malloc((size_t)width*3);if(!pixels||!rgb)gl_stop("capture allocation",bytes);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);gl_check("capture pixels");
    char path[1400];snprintf(path,sizeof path,"%s/frame-%08llu.ppm",root,(unsigned long long)L.presents);FILE *f=fopen(path,"wb");if(!f)gl_stop("capture file",errno);fprintf(f,"P6\n%u %u\n255\n",width,height);
    for(unsigned y=0;y<height;y++){const uint8_t *source=pixels+(size_t)(height-y-1)*row;for(unsigned x=0;x<width;x++)memcpy(rgb+x*3,source+x*4,3);if(fwrite(rgb,3,width,f)!=width)gl_stop("capture write",0);}if(fclose(f)||unlink(request))gl_stop("capture commit",errno);free(rgb);free(pixels);
}
static void gl_status_hud(unsigned output_width,unsigned output_height){
    static GLuint texture,fbo;static uint8_t *pixels;static unsigned width,height;
    unsigned w,h;if(!dm_linux_status_hud_extent(output_width,output_height,&w,&h))return;
    glActiveTexture(GL_TEXTURE0+L.max_tex-1);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_IMAGE_HEIGHT,0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_IMAGES,0);
    int resized=w!=width||h!=height;
    if(!texture){glGenTextures(1,&texture);glGenFramebuffers(1,&fbo);}
    glBindTexture(GL_TEXTURE_2D,texture);
    if(resized){
        uint8_t *next=realloc(pixels,(size_t)w*h*4);if(!next)gl_stop("status HUD allocation",(uint64_t)w*h*4);
        pixels=next;width=w;height=h;glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    }
    if(dm_linux_status_hud_pixels(pixels,width,height,0,gl_now())||resized)
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);glReadBuffer(GL_COLOR_ATTACHMENT0);
    if(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)gl_stop("status HUD framebuffer",0);
    glBlitFramebuffer(0,0,width,height,0,output_height,width,output_height-height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
}
uint64_t mtl_present(const tex_t *source,double period,const int crop[4]){
    (void)period;mtl_init();gl_enter();GlTexture *t=gl_texture(source,0);if(!t)gl_stop("presentation image unavailable",source?source->serial:0);
    gl_queries_end();glBindFramebuffer(GL_READ_FRAMEBUFFER,L.read_fbo);gl_attach(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,t,NULL);glReadBuffer(GL_COLOR_ATTACHMENT0);
    if(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)gl_stop("presentation framebuffer",source->serial);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);int width,height;dm_linux_gles_drawable_size(L.window,&width,&height);glDisable(GL_SCISSOR_TEST);
    int x=crop?crop[0]:0,y=crop?crop[1]:0,w=crop&&crop[2]>0?crop[2]:(int)source->d.width,h=crop&&crop[3]>0?crop[3]:(int)source->d.height;
    if(x<0||y<0||w<=0||h<=0||(uint64_t)x+w>source->d.width||(uint64_t)y+h>source->d.height)gl_stop("window crop bounds",w);
    if(L.vi_crop[2]>0&&L.vi_crop[3]>0){int right=x+w,bottom=y+h;if(L.vi_crop[0]>x)x=L.vi_crop[0];if(L.vi_crop[1]>y)y=L.vi_crop[1];
        if(L.vi_crop[0]+L.vi_crop[2]<right)right=L.vi_crop[0]+L.vi_crop[2];if(L.vi_crop[1]+L.vi_crop[3]<bottom)bottom=L.vi_crop[1]+L.vi_crop[3];w=right-x;h=bottom-y;}
    if(x<0||y<0||w<=0||h<=0||(uint64_t)x+w>source->d.width||(uint64_t)y+h>source->d.height||width<=0||height<=0)gl_stop("present crop bounds",w);
    int dw=width,dh=height;if((uint64_t)dw*h>(uint64_t)dh*w)dw=(int)((uint64_t)dh*w/h);else dh=(int)((uint64_t)dw*h/w);
    if(dw<1)dw=1;if(dh<1)dh=1;int dx=(width-dw)/2,dy=(height-dh)/2;
    if(dw!=width||dh!=height){glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);}
    glBlitFramebuffer(x,y,x+w,y+h,dx,L.origin?dy+dh:dy,dx+dw,L.origin?dy:dy+dh,GL_COLOR_BUFFER_BIT,GL_NEAREST);glBindFramebuffer(GL_FRAMEBUFFER,0);L.target_valid=0;
    static int previous[4];int rect[4]={x,y,w,h};if(!L.presents||memcmp(previous,rect,sizeof rect)){
        fprintf(stderr,"[nvn-gles] PRESENT_RECT crop=%d,%d,%d,%d texture=%u,%u destination=%d,%d,%d,%d output=%d,%d\n",x,y,w,h,source->d.width,source->d.height,dx,dy,dw,dh,width,height);memcpy(previous,rect,sizeof rect);}
    if(L.classic_enabled)dm_linux_native_hd_world_frame(L.classic_world_frame,L.presents+1);L.classic_world_frame=0;
    L.presents++;gl_status_hud((unsigned)width,(unsigned)height);gl_capture(width,height);SDL_GL_SwapWindow(L.window);gl_check("present");
    /* Present also reads the guest source image. The next translated fence
     * may publish the same submit ordinal, so complete that read before this
     * job returns. SwapWindow alone proves neither this GPU completion nor
     * physical scanout. Keep L.fence as the NVN submit ordinal. */
    uint64_t present_wait=gl_now();glFinish();L.gpu_wait_ns+=gl_now()-present_wait;
    fprintf(stderr,"[nvn-gles] GLES_TRANSFER presents=%llu buffer_bytes=%llu buffer_calls=%llu buffer_hits=%llu untracked_bytes=%llu vertex_snapshot_bytes=%llu vertex_snapshots=%llu sampler_snapshot_bytes=%llu sampler_snapshots=%llu writeback_bytes=%llu update_ms=%.3f readback_ms=%.3f gpu_wait_ms=%.3f readonly_snapshots=%llu readonly_bytes=%llu readonly_gpu_snapshots=%llu readonly_cache_hits=%llu\n",
        (unsigned long long)L.presents,(unsigned long long)L.buffer_upload_bytes,(unsigned long long)L.buffer_upload_calls,(unsigned long long)L.buffer_upload_hits,(unsigned long long)L.buffer_untracked_bytes,(unsigned long long)L.snapshot_bytes[0],(unsigned long long)L.snapshot_calls[0],(unsigned long long)L.snapshot_bytes[1],(unsigned long long)L.snapshot_calls[1],(unsigned long long)L.writeback_bytes,L.pool_update_ns/1e6,L.readback_ns/1e6,L.gpu_wait_ns/1e6,(unsigned long long)L.readonly_snapshot_calls,(unsigned long long)L.readonly_snapshot_bytes,(unsigned long long)L.readonly_gpu_snapshots,(unsigned long long)L.readonly_cache_hits);
    if(L.resource_reuse&&(L.presents==1||!(L.presents%60)))fprintf(stderr,"[nvn-gles] GLES_RESOURCE_PROFILE presents=%llu cached_bytes=%llu budget_bytes=%llu hits=%llu partial_updates=%llu evictions=%llu\n",(unsigned long long)L.presents,(unsigned long long)L.resource_bytes,(unsigned long long)GL_RESOURCE_BUDGET,(unsigned long long)L.resource_hits,(unsigned long long)L.resource_partial,(unsigned long long)L.resource_evictions);
    if(L.presents==1||!(L.presents%60))fprintf(stderr,"[nvn-gles] GLES_TARGET_PROFILE presents=%llu retained=%llu rebuilt=%llu\n",(unsigned long long)L.presents,(unsigned long long)L.target_hits,(unsigned long long)L.target_misses);
    if(L.presents==1||!(L.presents%60))fprintf(stderr,"[nvn-gles] GLES_UNIFORM_PROFILE presents=%llu enabled=%d deferred=%llu flushes=%llu updates=%llu snapshots=%llu snapshot_bytes=%llu cache_hits=%llu\n",(unsigned long long)L.presents,L.uniform_snapshot,(unsigned long long)L.uniform_deferred,(unsigned long long)L.uniform_flushes,(unsigned long long)L.uniform_updates,(unsigned long long)L.uniform_snapshots,(unsigned long long)L.uniform_snapshot_bytes,(unsigned long long)L.uniform_cache_hits);
    if(L.presents==1||!(L.presents%60))fprintf(stderr,"[nvn-gles] GLES_COHERENT_PROFILE presents=%llu enabled=%d api_barriers=%llu readonly_draw_barriers=%llu shader_invalidation_barriers=%llu writer_barriers=%llu\n",(unsigned long long)L.presents,L.coherent_reads,(unsigned long long)L.coherent_api_barriers,(unsigned long long)L.coherent_draw_barriers,(unsigned long long)L.coherent_guest_barriers,(unsigned long long)L.coherent_writer_barriers);
    if(L.presents==1||!(L.presents%60)){
        fprintf(stderr,"[nvn-gles] GLES_REUSE_PROFILE presents=%llu menu_reuses=%llu menu_warm=%llu menu_fallbacks=%llu equal_copies=%llu equal_copy_bytes=%llu\n",(unsigned long long)L.presents,(unsigned long long)L.menu_reuses,(unsigned long long)L.menu_warm,(unsigned long long)L.menu_fallbacks,(unsigned long long)L.cpu_equal_copies,(unsigned long long)L.cpu_equal_copy_bytes);
        fprintf(stderr,"[nvn-gles] GLES_DOWNLOAD_PROFILE presents=%llu staged_commits=%llu staged_bytes=%llu staged_maps=%llu direct_maps=%llu budget_bytes=%llu\n",(unsigned long long)L.presents,(unsigned long long)L.download_calls,(unsigned long long)L.download_bytes,(unsigned long long)L.download_maps,(unsigned long long)L.direct_readback_maps,(unsigned long long)GL_DOWNLOAD_BUDGET);
    }
    if(L.presents==1||!(L.presents%60))fprintf(stderr,"[nvn-gles] GLES_PLAN_PROFILE presents=%llu counter_omitted=%llu classic_draws=%llu classic_clears=%llu classic_copies=%llu classic_dispatches=%llu fallbacks=%llu\n",(unsigned long long)L.presents,(unsigned long long)L.counter_omitted,(unsigned long long)L.classic_draws,(unsigned long long)L.classic_clears,(unsigned long long)L.classic_copies,(unsigned long long)L.classic_dispatches,(unsigned long long)L.plan_fallbacks);
    if(L.presents==1||!(L.presents%60))fprintf(stderr,"[nvn-gles] GLES_ASTC_COPY_PROFILE presents=%llu copies=%llu texels=%llu\n",(unsigned long long)L.presents,(unsigned long long)L.astc_copies,(unsigned long long)L.astc_texels);
    if(L.presents==1||!(L.presents%60))fprintf(stderr,"[nvn-gles] GLES_DIRECT_PROFILE presents=%llu staged_updates=%llu staged_bytes=%llu direct_sampler_calls=%llu direct_sampler_bytes=%llu\n",(unsigned long long)L.presents,(unsigned long long)L.staged_update_calls,(unsigned long long)L.staged_update_bytes,(unsigned long long)L.direct_sampler_calls,(unsigned long long)L.direct_sampler_bytes);
    /* Cold/blocked scenes may never reach the 60-frame performance window. */
    if(L.presents==1){fprintf(stderr,"[nvn-gles] GLES_PROGRESS presents=%llu draws=%llu skipped_draws=%llu dispatches=%llu skipped_dispatches=%llu filtered_total=%llu fidelity=%s\n",
        (unsigned long long)L.presents,(unsigned long long)L.draws,(unsigned long long)L.skipped_draws,(unsigned long long)L.dispatches,(unsigned long long)L.skipped_dispatches,(unsigned long long)L.filtered,L.filtered?"degraded":"unverified");gl_filter_profile();gl_memory_profile();}
    if(!(L.presents%60)){uint64_t now=gl_now();double ms=(now-L.profile_start)/1e6;
        fprintf(stderr,"[nvn-gles] GLES_PROFILE frames=60 fps=%.3f elapsed_ms=%.3f draws=%llu skipped_draws=%llu dispatches=%llu skipped_dispatches=%llu filtered_total=%llu shader_compiles=%llu fidelity=%s\n",60000/ms,ms,(unsigned long long)(L.draws-L.profile_draws),(unsigned long long)(L.skipped_draws-L.profile_skip),(unsigned long long)(L.dispatches-L.profile_dispatches),(unsigned long long)(L.skipped_dispatches-L.profile_dispatch_skip),(unsigned long long)L.filtered,(unsigned long long)L.compiles,L.filtered?"degraded":"unverified");
        L.profile_start=now;L.profile_draws=L.draws;L.profile_skip=L.skipped_draws;L.profile_dispatches=L.dispatches;L.profile_dispatch_skip=L.skipped_dispatches;}
    if(!(L.presents%60))gl_filter_profile();
    dm_android_front_presented(L.presents);uint64_t n=L.presents;gl_leave();return n;
}
void mtl_present_wait(uint64_t n){if(n>L.presents)gl_stop("presentation ticket",n);}
void mtl_present_flush(void){mtl_finish();}
double mtl_presented_time(uint64_t n){(void)n;return 0;}
void dm_vk_linux_display_probe(void){mtl_init();gl_enter();glBindFramebuffer(GL_FRAMEBUFFER,0);glClearColor(.1f,.3f,.5f,1);glClear(GL_COLOR_BUFFER_BIT);SDL_GL_SwapWindow(L.window);gl_check("display probe");gl_leave();}
int dm_vk_linux_keyboard_size(void *window,int *w,int *h){dm_linux_gles_drawable_size(window,w,h);return *w>0&&*h>0;}
int dm_vk_linux_keyboard_present(void *window,const void *rgba,int w,int h,int pitch){(void)window;(void)rgba;(void)w;(void)h;(void)pitch;return gl_filter("keyboard_overlay_unavailable",0);}
void dm_vk_linux_keyboard_close(void *window){(void)window;}
void mtl_read_texels(const tex_t *t,unsigned level,unsigned layer,const NVNcopyRegion *r,uint8_t *dst,size_t row,size_t img){(void)t;(void)level;(void)layer;(void)r;(void)dst;(void)row;(void)img;gl_stop("CPU texture readback not implemented in GLES experiment",0);}
#include "nvn_gl_cpu_texture.inc"
