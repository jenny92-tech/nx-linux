// INPUT: Exact CPU baselines and later guest writes to block-linear ASTC storage.
// OUTPUT: Changed compression blocks uploaded while GPU-only blocks remain intact.
// POS: GLES texture coherence for allocations shared by CPU streaming and GPU copies.
static void gl_astc_baseline(GlTexture *t,uint8_t *snapshot){
    L.astc_baseline_bytes-=t->cpu_snapshot_size;free(t->cpu_snapshot);
    t->cpu_snapshot_size=snapshot?nvn_vk_snapshot_pack(&snapshot,t->source.L.size):0;
    t->cpu_snapshot=snapshot;L.astc_baseline_bytes+=t->cpu_snapshot_size;
}
static void gl_refresh_astc_cpu(GlTexture *t,const tex_t *source){
    uint64_t stamp=nvn_dirty_now();nvn_dirty_protect(source->mem,source->L.size);
    uint8_t *snapshot=malloc(source->L.size),*scratch=NULL;
    if(!snapshot)gl_stop("ASTC streamed baseline allocation",source->L.size);
    memcpy(snapshot,source->mem,source->L.size);
    const uint8_t *baseline=nvn_vk_snapshot_open(t->cpu_snapshot,t->cpu_snapshot_size,source->L.size,&scratch);
    if(!baseline)gl_stop("ASTC streamed baseline decode",source->serial);
    uint64_t changed=0;glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glBindTexture(t->target,t->name);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    for(unsigned layer=0;layer<source->L.layers;layer++)for(unsigned level=0;level<source->d.levels;level++){
        uint32_t row,rows,slices,block;uint64_t base;unsigned th,td;
        if(nvn_deswizzle_geom(&source->d,level,layer,&row,&rows,&slices,&base,&th,&td,&block))gl_stop("ASTC streamed layout",source->serial);
        size_t bytes=(size_t)row*rows*slices;uint8_t *old=malloc(bytes),*next=malloc(bytes);
        if(!old||!next)gl_stop("ASTC streamed block comparison allocation",bytes);
        if(nvn_deswizzle(&source->d,level,layer,baseline,old,row)||nvn_deswizzle(&source->d,level,layer,snapshot,next,row))gl_stop("ASTC streamed deswizzle",source->serial);
        int bw,bh;if(nvn_fmt_bpb(source->d.format,&bw,&bh)!=(int)block)gl_stop("ASTC streamed block size",block);
        unsigned width=gl_dim(source->d.width,level),height=gl_dim(source->d.height,level),columns=row/block;
        for(unsigned z=0;z<slices;z++)for(unsigned y=0;y<rows;y++)for(unsigned x=0;x<columns;){
            size_t offset=((size_t)z*rows+y)*row+(size_t)x*block;
            if(!memcmp(old+offset,next+offset,block)){x++;continue;}
            unsigned first=x++;while(x<columns&&memcmp(old+((size_t)z*rows+y)*row+(size_t)x*block,next+((size_t)z*rows+y)*row+(size_t)x*block,block))x++;
            unsigned px=first*bw,py=y*bh,w=(x-first)*bw,h=bh;if(w>width-px)w=width-px;if(h>height-py)h=height-py;
            size_t size=(size_t)(x-first)*block;GLenum target=t->target==GL_TEXTURE_CUBE_MAP?GL_TEXTURE_CUBE_MAP_POSITIVE_X+layer:t->target;
            if(gl_storage_3d(t->target))glCompressedTexSubImage3D(target,level,px,py,t->target==GL_TEXTURE_3D?z:layer,w,h,1,t->internal,size,next+offset);
            else glCompressedTexSubImage2D(target,level,px,py,w,h,t->internal,size,next+offset);
            changed+=x-first;
        }
        free(old);free(next);
    }
    GLenum error=glGetError();if(error)gl_stop("ASTC streamed upload",error);
    free(scratch);gl_astc_baseline(t,snapshot);t->stamp=stamp;
    if(changed){t->version++;L.uploads++;L.astc_refreshes++;L.astc_refresh_blocks+=changed;
        if(L.astc_refreshes<=16)fprintf(stderr,"[nvn-gles] ASTC_STREAM_REFRESH serial=%llu blocks=%llu gpu_blocks_preserved=1\n",(unsigned long long)source->serial,(unsigned long long)changed);}
}
