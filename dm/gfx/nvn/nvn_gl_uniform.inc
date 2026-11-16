// INPUT: Immutable inline uniform updates and complete submission write history.
// OUTPUT: Per-draw read versions and ordered canonical bytes at real observers.
// POS: Optional GLES counterpart of Vulkan's dense-pool uniform snapshots.
#define GL_UNIFORM_BUDGET (1024u*1024u)
static int gl_uniform_pending_overlap(GlRange r){
    for(unsigned i=0;i<L.nuniform_pending;i++)if(overlap(r,L.writes[L.uniform_pending[i]]))return 1;
    return 0;
}
static void gl_uniform_flush_all(void){
    if(!L.nuniform_pending)return;
    for(unsigned i=0;i<L.nuniform_pending;i++){
        GlRange r=L.writes[L.uniform_pending[i]];GlPool *p=gl_pool(r.addr,r.size,NULL);
        if(!p)gl_stop("deferred uniform pool lifetime",r.addr);
        L.uniform_plan[i]=(NvnVkWritebackSpan){r.addr,r.size,p->serial};
    }
    size_t count=nvn_vk_writeback_plan(L.uniform_plan,L.nuniform_pending);
    if(count==SIZE_MAX)gl_stop("uniform merge plan",L.nuniform_pending);
    gl_api_memory_barrier(GL_ALL_BARRIER_BITS);
    for(size_t i=0;i<count;i++){
        NvnVkWritebackSpan span=L.uniform_plan[i];uint64_t offset=0;GlPool *p=gl_pool(span.addr,span.size,&offset);
        if(!p||p->serial!=span.pool_serial||span.size>GL_UNIFORM_BUDGET)gl_stop("uniform merge storage",span.addr);
        uint8_t *bytes=malloc(span.size);if(!bytes)gl_stop("uniform merge allocation",span.size);
        /* The union contains no gaps. Only pending updates are merged here:
         * every intervening non-uniform observer has already forced a flush.
         * Preserve partial overlaps in issue order; the last byte writer wins. */
        for(unsigned j=0;j<L.nuniform_pending;j++){
            unsigned index=L.uniform_pending[j];GlRange w=L.writes[index];
            if(!overlap((GlRange){span.addr,span.size},w))continue;
            uint64_t lo=span.addr>w.addr?span.addr:w.addr,hi=span.addr+span.size<w.addr+w.size?span.addr+span.size:w.addr+w.size;
            memcpy(bytes+lo-span.addr,L.write_cpu[index]+lo-w.addr,hi-lo);
        }
        gl_backing(p,offset+span.size);gl_pool_update(p,offset,span.size,bytes);free(bytes);L.uniform_updates++;
    }
    /* A copy can call gl_upload after issuing its own barrier. The final
     * delayed update must still be visible to that immediate GPU consumer. */
    gl_api_memory_barrier(GL_ALL_BARRIER_BITS);L.nuniform_pending=0;L.uniform_pending_bytes=0;L.uniform_flushes++;
}
static void gl_uniform_flush_range(GlRange r){if(gl_uniform_pending_overlap(r))gl_uniform_flush_all();}
static int gl_uniform_defer(GlRange r,const uint8_t *bytes){
    uint64_t offset=0;GlPool *p=gl_pool(r.addr,r.size,&offset);
    if(!L.uniform_snapshot||!L.submission_active||!p||!r.size||r.size>65536||r.size%4||offset%4||
       (p->flags&(NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS|NVN_MEMORY_POOL_FLAGS_COMPRESSIBLE)))return 0;
    if(L.nuniform_pending==GL_WRITES||r.size>GL_UNIFORM_BUDGET-L.uniform_pending_bytes)gl_uniform_flush_all();
    unsigned index=L.nwrites;gl_mark(r);L.write_cpu[index]=bytes;L.uniform_pending[L.nuniform_pending++]=index;
    L.uniform_pending_bytes+=r.size;L.uniform_deferred++;return 1;
}
static int gl_uniform_read_snapshot(GlRange r,GLuint *buffer){
    if(!L.uniform_snapshot||!L.readonly_draw||!r.size||r.size>65536)return 0;
    int cpu_write=0;
    for(unsigned i=0;i<L.nwrites;i++)if(overlap(r,L.writes[i])){
        if(!L.write_cpu[i])return 0;cpu_write=1;
    }
    if(!cpu_write)return 0;
    GlPool *p=gl_pool(r.addr,r.size,NULL);
    if(!p||(p->flags&(NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS|NVN_MEMORY_POOL_FLAGS_COMPRESSIBLE)))return 0;
    uint64_t hash=(r.addr>>4)^(r.addr>>19)^(r.size*0x9e3779b97f4a7c15ull)^p->serial;
    GlReadSnapshot *cached=&L.read_cache[hash&255];GlUpload *source=&cached->source;
    int tracked=nvn_dirty_tracked((void *)(uintptr_t)r.addr,r.size);uint64_t stamp=0;
    if(tracked&&cached->buffer&&source->pool_serial==p->serial&&source->range.addr==r.addr&&source->range.size==r.size&&!nvn_dirty_since((void *)(uintptr_t)r.addr,r.size,source->stamp)){
        *buffer=cached->buffer;L.uniform_cache_hits++;return 1;
    }
    if(tracked){stamp=nvn_dirty_now();nvn_dirty_protect((void *)(uintptr_t)r.addr,r.size);}
    uint8_t *bytes=malloc(r.size);if(!bytes)gl_stop("uniform snapshot allocation",r.size);memcpy(bytes,(void *)(uintptr_t)r.addr,r.size);
    /* Retain the entire history, even after flush. Guest RAM is published only
     * at completion, and a previous GPU producer always forbids CPU overlay. */
    for(unsigned i=0;i<L.nwrites;i++)if(L.write_cpu[i]&&overlap(r,L.writes[i])){
        GlRange w=L.writes[i];uint64_t lo=r.addr>w.addr?r.addr:w.addr,hi=r.addr+r.size<w.addr+w.size?r.addr+r.size:w.addr+w.size;
        memcpy(bytes+lo-r.addr,L.write_cpu[i]+lo-w.addr,hi-lo);
    }
    *buffer=gl_temp(r.size,bytes);free(bytes);
    if(tracked)*cached=(GlReadSnapshot){{r,p->serial,stamp},*buffer};
    L.uniform_snapshots++;L.uniform_snapshot_bytes+=r.size;L.readonly_snapshot_calls++;L.readonly_snapshot_bytes+=r.size;return 1;
}
static int gl_uniform_opcode_keeps(uint32_t op){
    switch(op){
    case OP_BEGIN:case OP_END:case OP_CALL:case OP_JUMP:case OP_UPDATE_UB:
    case OP_SET_RT:case OP_VIEWPORTS:case OP_SCISSOR:case OP_DEPTH_RANGE:case OP_BIND_PROGRAM:case OP_BIND_STATE:
    case OP_BIND_VATTR:case OP_BIND_VSTREAM:case OP_BIND_VBUF:case OP_BIND_UBO:case OP_BIND_SSBO:
    case OP_BIND_TEX:case OP_BIND_SAMP:case OP_BIND_IMG:case OP_STENCIL:case OP_POLY_OFFSET:case OP_BLEND_COLOR:
    case OP_DRAW:case OP_DRAW_IDX:case OP_DRAW_INSTANCED:case OP_BARRIER:
    case OP_SET_TEX_POOL:case OP_SET_SAMP_POOL:case OP_COPY_STRIDE:case OP_COPY_IMAGE_STRIDE:case OP_SHADER_SCRATCH:case OP_DEBUG:return 1;
    default:return 0;
    }
}
