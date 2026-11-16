// INPUT: Synchronous NVN CPU writes, including byte strides and mip/layer views.
// OUTPUT: Matching guest block-linear bytes and partial GLES image updates.
// POS: CPU texture writes in the optional GLES backend; GPU-only neighbours survive.
void dm_vk_write_texels(const tex_t *source,const view_t *view,const NVNcopyRegion *r,const void *data,ptrdiff_t row_stride,ptrdiff_t image_stride){
    if(!source||!r||!data)gl_stop("CPU texture write arguments",0);
    unsigned level=view&&view->set_levels?view->base_level:0,layer=view&&view->set_layers?view->min_layer:0;
    int bw=0,bh=0,bytes=nvn_fmt_bpb(source->d.format,&bw,&bh),decoded_bpp;
    uint32_t decoded;int bc=nvn_bc_unpacked(source->d.format,&decoded,&decoded_bpp);
    int volume=source->d.target==NVN_TEXTURE_TARGET_3D,one=source->d.target==NVN_TEXTURE_TARGET_1D;
    if((source->d.target!=NVN_TEXTURE_TARGET_2D&&source->d.target!=NVN_TEXTURE_TARGET_2D_ARRAY&&source->d.target!=NVN_TEXTURE_TARGET_CUBEMAP&&source->d.target!=NVN_TEXTURE_TARGET_CUBEMAP_ARRAY&&!volume&&!one)||
       !bytes||(!bc&&(bw!=1||bh!=1))||source->d.samples>1||
       (source->d.format>=NVN_FORMAT_DEPTH16&&source->d.format<=NVN_FORMAT_DEPTH32F_STENCIL8)||
       (view&&view->set_format&&view->format!=source->d.format)||(view&&view->set_target&&view->target!=source->d.target)){
        fprintf(stderr,"[nvn-gles] CPU_TEXTURE_WRITE_UNSUPPORTED target=%u format=%u samples=%u region=%d,%d,%d,%d,%d,%d\n",source->d.target,source->d.format,source->d.samples,r->xoffset,r->yoffset,r->zoffset,r->width,r->height,r->depth);
        gl_stop("CPU texture write target/format",source->d.format);
    }
    if(level>=source->d.levels||layer>=source->L.layers||r->xoffset<0||r->yoffset<0||r->zoffset<0||r->width<0||r->height<0||r->depth<0||(!volume&&(r->zoffset||r->depth!=1)))gl_stop("CPU texture write region",level);
    unsigned width=gl_dim(source->d.width,level),height=one?1:gl_dim(source->d.height,level),depth=volume?gl_dim(source->d.depth,level):1;
    if((uint64_t)r->xoffset+r->width>width||(uint64_t)r->yoffset+r->height>height||(uint64_t)r->zoffset+r->depth>depth)gl_stop("CPU texture write bounds",source->serial);
    if(!r->width||!r->height||!r->depth)return;
    if(r->xoffset%bw||r->yoffset%bh||(r->width%bw&&(uint64_t)r->xoffset+r->width!=width)||(r->height%bh&&(uint64_t)r->yoffset+r->height!=height))gl_stop("CPU texture write block alignment",source->d.format);
    size_t row,image,extent;
    if(!nvn_copy_pitch(r->width,r->height,r->depth,bw,bh,bytes,row_stride,image_stride,&row,&image,&extent))gl_stop("CPU texture write stride",source->serial);
    size_t rows=((size_t)r->height+bh-1)/bh,pitch=(((size_t)r->width+bw-1)/bw)*bytes;
    size_t packed_size=pitch*rows*r->depth;uint8_t *packed=malloc(packed_size);
    if(!packed)gl_stop("CPU texture write allocation",packed_size);
    /* Snapshot first: the caller may point into the destination allocation. */
    for(int z=0;z<r->depth;z++)for(size_t y=0;y<rows;y++)memcpy(packed+((size_t)z*rows+y)*pitch,(const uint8_t *)data+(size_t)z*image+y*row,pitch);
    uint32_t full_row,full_rows,slices,block;uint64_t base;unsigned th,td;
    if(nvn_deswizzle_geom(&source->d,level,layer,&full_row,&full_rows,&slices,&base,&th,&td,&block))gl_stop("CPU texture write layout",source->serial);
    mtl_init();gl_enter();if(L.submission_active)gl_stop("CPU texture write during submission",source->serial);
    if(!gl_pool((uint64_t)(uintptr_t)source->mem,source->L.size,NULL))gl_stop("CPU texture write storage",source->serial);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_IMAGE_HEIGHT,0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_IMAGES,0);
    /* Upload the initial image before editing RAM. Never replace GPU-owned
     * pixels outside the addressed region with stale guest bytes. */
    GlTexture *t=gl_texture(source,1);if(!t)gl_stop("CPU texture write image unavailable",source->serial);
    uint8_t *pixels=packed;size_t upload_row=pitch;
    if(bc||t->unorm16_float){
        upload_row=(size_t)r->width*t->bpp;
        if(upload_row>SIZE_MAX/(size_t)r->height/(size_t)r->depth)gl_stop("CPU texture decode size",source->serial);
        size_t size=upload_row*r->height*r->depth;pixels=malloc(size);if(!pixels)gl_stop("CPU texture decode allocation",size);
        if(t->unorm16_float){for(size_t i=0;i<(size_t)r->width*r->height*r->depth;i++){uint16_t value;memcpy(&value,packed+i*2,2);((float *)pixels)[i]=(float)value/65535.0f;}}
        else for(int z=0;z<r->depth;z++)if(nvn_bc_decode(source->d.format,packed+(size_t)z*pitch*rows,pitch,r->width,r->height,pixels+(size_t)z*upload_row*r->height,upload_row))gl_stop("CPU texture BC decode",source->d.format);
    }
    for(int z=0;z<r->depth;z++)for(size_t y=0;y<rows;y++)for(size_t x=0;x<pitch;x++){
        uint64_t offset=base+nvn_bl_offset((uint32_t)((size_t)(r->xoffset/bw)*bytes+x),r->yoffset/bh+y,r->zoffset+z,full_row,full_rows,th,td);
        if(offset>=source->L.size)gl_stop("CPU texture write storage bounds",offset);
        source->mem[offset]=packed[((size_t)z*rows+y)*pitch+x];
    }
    glBindTexture(t->target,t->name);
    if(gl_storage_3d(t->target))glTexSubImage3D(t->target,level,r->xoffset,r->yoffset,volume?r->zoffset:(int)layer,r->width,r->height,r->depth,t->format,t->type,pixels);
    else glTexSubImage2D(t->target==GL_TEXTURE_CUBE_MAP?GL_TEXTURE_CUBE_MAP_POSITIVE_X+layer:t->target,level,r->xoffset,r->yoffset,r->width,r->height,t->format,t->type,pixels);
    gl_check("CPU texture partial upload");
    /* A mirrored CPU upload does not transfer ownership. CPU-owned images
     * still need ordinary dirty-pool refreshes; GPU-owned ones retain pixels
     * outside this region and stay GPU-owned. */
    t->version++;t->stamp=nvn_dirty_now();nvn_dirty_protect(source->mem,source->L.size);L.uploads++;
    /* This call is outside the submit ordinal. Complete it before a following
     * queue fence can publish completion of the previously drained submission. */
    glFinish();gl_check("CPU texture write completion");
    if(pixels!=packed)free(pixels);free(packed);gl_leave();
}
