// INPUT: Buffer identities, dirty generations and authoritative GPU backing.
// OUTPUT: Bounded read textures with CPU dirty patches and fresh GPU-written spans.
// POS: GLES adapter for the backend-independent transfer-span planner.
static void gl_resource_discard(GlResource *r){
    if(!r->texture)return;
    glDeleteTextures(1,&r->texture);L.resource_bytes-=r->bytes;memset(r,0,sizeof *r);L.resource_evictions++;
}
static void gl_resource_forget(uint64_t serial){
    for(unsigned i=0;i<GL_RESOURCE_ENTRIES;i++)if(L.resources[i].source.pool_serial==serial)gl_resource_discard(&L.resources[i]);
}
static GlResource *gl_resource_slot(uint64_t bytes){
    if(bytes>GL_RESOURCE_BUDGET)return NULL;
    for(;;){GlResource *empty=NULL,*oldest=NULL;
        for(unsigned i=0;i<GL_RESOURCE_ENTRIES;i++){GlResource *r=&L.resources[i];
            if(!r->texture){if(!empty)empty=r;}
            else if(!r->pinned&&(!oldest||r->age<oldest->age))oldest=r;
        }
        if(empty&&L.resource_bytes<=GL_RESOURCE_BUDGET-bytes)return empty;
        if(!oldest)return NULL;
        gl_resource_discard(oldest);
    }
}
typedef struct {GlRange range;uint64_t offset,copied;unsigned bpp,width;GLenum format,type;} GlTextureTransfer;
static void gl_emit_texture_span(void *context,uint64_t addr,uint64_t size){
    GlTextureTransfer *copy=context;uint64_t first=(addr-copy->range.addr)/copy->bpp;
    uint64_t end=(addr-copy->range.addr+size+copy->bpp-1)/copy->bpp;
    while(first<end){unsigned x=first%copy->width,y=first/copy->width;
        uint64_t texels=end-first;unsigned width,height=1;
        if(x){width=copy->width-x;if(width>texels)width=(unsigned)texels;}
        else if(texels>=copy->width){width=copy->width;height=(unsigned)(texels/copy->width);}
        else width=(unsigned)texels;
        glTexSubImage2D(GL_TEXTURE_2D,0,x,y,width,height,copy->format,copy->type,(const void *)(uintptr_t)(copy->offset+first*copy->bpp));
        uint64_t used=(uint64_t)width*height;copy->copied+=used*copy->bpp;first+=used;
    }
}
static GLuint gl_resource_snapshot(GlRange range,GLenum internal,GLenum format,GLenum type,unsigned bpp,unsigned kind){
    gl_uniform_flush_range(range);
    if(!range.size||!bpp||range.size%bpp||kind>1)gl_stop("resource snapshot range",range.size);
    GlPool *pool=gl_pool(range.addr,range.size,NULL);if(!pool)gl_stop("resource snapshot pool",range.addr);
    uint64_t texels=range.size/bpp,height=(texels+2047)/2048;unsigned width=texels<2048?(unsigned)texels:2048;
    if(height>(uint64_t)L.max_texture2d)return gl_filter("resource snapshot height",height);
    int owned=0,aligned=1;
    for(unsigned i=0;i<L.nwrites;i++)if(overlap(range,L.writes[i])){
        GlRange w=L.writes[i];uint64_t lo=w.addr>range.addr?w.addr:range.addr,end=w.addr+w.size;
        if(end>range.addr+range.size)end=range.addr+range.size;
        owned=1;if((lo-range.addr)%bpp||(end-range.addr)%bpp)aligned=0;
    }
    int tracked=nvn_dirty_tracked((const void *)(uintptr_t)range.addr,range.size);
    GlResource *found=NULL;
    if(tracked)for(unsigned i=0;i<GL_RESOURCE_ENTRIES;i++){GlResource *r=&L.resources[i];
        if(r->texture&&r->source.pool_serial==pool->serial&&r->source.range.addr==range.addr&&r->source.range.size==range.size&&
           r->internal==internal&&r->format==format&&r->type==type&&r->bpp==bpp&&(!found||r->age>found->age))found=r;
    }
    if(found&&!owned&&!nvn_dirty_since((const void *)(uintptr_t)range.addr,range.size,found->source.stamp)){
        found->pinned=1;found->age=++L.resource_age;L.resource_hits++;return found->texture;
    }
    /* A referenced texture must stay immutable until the real submission
     * fence. Dirty versions in the same submission get separate storage. */
    int partial=found&&!found->pinned;
    uint64_t old_stamp=partial?found->source.stamp:0,stamp=0,bytes=(uint64_t)width*height*bpp;
    GlResource *entry=partial?found:tracked?gl_resource_slot(bytes):NULL;
    GLuint texture;
    if(partial)texture=entry->texture;
    else{
        if(entry){memset(entry,0,sizeof *entry);glGenTextures(1,&entry->texture);texture=entry->texture;entry->bytes=bytes;L.resource_bytes+=bytes;}
        else{if(L.ntemp==GL_WRITES)gl_stop("resource snapshot temporaries",L.ntemp);GlTemp *t=&L.temps[L.ntemp++];*t=(GlTemp){0};glGenTextures(1,&t->texture);texture=t->texture;}
    }
    glActiveTexture(GL_TEXTURE0+L.max_tex-1);glBindTexture(GL_TEXTURE_2D,texture);
    if(!partial){glTexStorage2D(GL_TEXTURE_2D,1,internal,width,(GLsizei)height);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);}
    if(tracked){stamp=nvn_dirty_now();nvn_dirty_protect((const void *)(uintptr_t)range.addr,range.size);}
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    GlTextureTransfer copy={range,range.addr,0,bpp,width,format,type};
    if(!aligned){
        /* A partial GPU-written texel cannot be reconstructed from stale CPU
         * bytes. The authoritative pool merge handles this uncommon shape. */
        uint64_t offset;pool=gl_upload(range,&offset);copy.offset=offset;
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pool->backing.name);
        gl_emit_texture_span(&copy,range.addr,range.size);
    }else{
        /* CPU-owned bytes go directly to the small read texture. Updating a
         * giant pool first can make a driver rename/copy that whole store. */
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        if(nvn_transfer_visit(range.addr,range.size,L.writes,L.nwrites,old_stamp,partial?nvn_dirty_next_range:NULL,gl_emit_texture_span,&copy)==UINT64_MAX)gl_stop("resource CPU snapshot plan",range.addr);
        if(owned){
            if(!pool->backing.name)gl_stop("resource GPU backing",range.addr);
            copy.offset=range.addr-(uint64_t)(uintptr_t)pool->guest;
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pool->backing.name);
            glMemoryBarrier(GL_PIXEL_BUFFER_BARRIER_BIT|GL_BUFFER_UPDATE_BARRIER_BIT);
            if(nvn_transfer_visit_written(range.addr,range.size,L.writes,L.nwrites,gl_emit_texture_span,&copy)==UINT64_MAX)gl_stop("resource GPU snapshot plan",range.addr);
        }
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    GLenum error=glGetError();if(error){if(entry)gl_resource_discard(entry);gl_filter("resource_snapshot",error);return 0;}
    if(entry){entry->source=(GlUpload){range,pool->serial,stamp};entry->internal=internal;entry->format=format;entry->type=type;entry->bpp=bpp;entry->width=width;entry->height=(unsigned)height;entry->pinned=1;entry->age=++L.resource_age;}
    static unsigned traced;if(traced++<16)fprintf(stderr,"[nvn-gles] RESOURCE_TRACE pool=%llu offset=%llu size=%llu gpu_owned=%d tracked=%d found=%d partial=%d copied=%llu cached_bytes=%llu evictions=%llu\n",(unsigned long long)pool->serial,(unsigned long long)(range.addr-(uint64_t)(uintptr_t)pool->guest),(unsigned long long)range.size,owned,tracked,found!=NULL,partial,(unsigned long long)copy.copied,(unsigned long long)L.resource_bytes,(unsigned long long)L.resource_evictions);
    if(partial)L.resource_partial++;
    L.snapshot_calls[kind]++;L.snapshot_bytes[kind]+=copy.copied;return texture;
}
