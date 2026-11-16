// INPUT: Live pools, submission writes, dirty generations and a GLES context.
// OUTPUT: Small immutable read buffers using current CPU or GPU contents.
// POS: Submission-owned read snapshots and bounded lookup cache for GLES.
static GLuint gl_temp(size_t bytes,const void *data){
    if(!bytes||bytes>512ull*1024*1024||L.ntemp==GL_WRITES)gl_stop("temporary buffer bounds",bytes);
    GlTemp *t=&L.temps[L.ntemp++];*t=(GlTemp){0};glGenBuffers(1,&t->buffer);glBindBuffer(GL_COPY_WRITE_BUFFER,t->buffer);glBufferData(GL_COPY_WRITE_BUFFER,bytes,data,GL_STREAM_DRAW);return t->buffer;
}
/* Read-only small bindings get their own submission-owned bytes. A CPU
 * snapshot is current only if this submission has not written the range;
 * otherwise copy from the authoritative GPU backing, preserving mixed ranges. */
static GLuint gl_read_buffer(GlRange r,uint64_t *offset){
    GlPool *pool=gl_pool(r.addr,r.size,NULL);if(!pool)gl_stop("read buffer outside live pool",r.addr);
    GLuint uniform;if(gl_uniform_read_snapshot(r,&uniform)){*offset=0;return uniform;}
    if(L.uniform_snapshot&&!L.readonly_draw){GlPool *p=gl_upload(r,offset);return p->backing.name;}
    int gpu_written=0;for(unsigned i=0;i<L.nwrites;i++)if(overlap(r,L.writes[i])){gpu_written=1;break;}
    /* CPU-only read bindings up to 1 MiB need no update to a giant pool.
     * Large GPU-owned bindings retain their authoritative direct backing. */
    if(r.size&&(r.size<=65536||(r.size<=1024*1024&&!gpu_written))){
        GLuint buffer;
        if(!gpu_written){
            uint64_t hash=(r.addr>>4)^(r.addr>>19)^(r.size*0x9e3779b97f4a7c15ull)^pool->serial,stamp=0;
            GlReadSnapshot *cached=&L.read_cache[hash&255];GlUpload *source=&cached->source;
            int tracked=nvn_dirty_tracked((void *)(uintptr_t)r.addr,r.size);
            if(tracked&&cached->buffer&&source->pool_serial==pool->serial&&source->range.addr==r.addr&&source->range.size==r.size&&!nvn_dirty_since((void *)(uintptr_t)r.addr,r.size,source->stamp)){L.readonly_cache_hits++;*offset=0;return cached->buffer;}
            if(tracked){stamp=nvn_dirty_now();nvn_dirty_protect((void *)(uintptr_t)r.addr,r.size);}
            buffer=gl_temp(r.size,(void *)(uintptr_t)r.addr);
            if(tracked)*cached=(GlReadSnapshot){{r,pool->serial,stamp},buffer};
        }else{uint64_t source_offset;GlPool *p=gl_upload(r,&source_offset);buffer=gl_temp(r.size,NULL);
            glBindBuffer(GL_COPY_READ_BUFFER,p->backing.name);glBindBuffer(GL_COPY_WRITE_BUFFER,buffer);
            gl_api_memory_barrier(GL_BUFFER_UPDATE_BARRIER_BIT);glCopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,source_offset,0,r.size);L.readonly_gpu_snapshots++;}
        L.readonly_snapshot_calls++;L.readonly_snapshot_bytes+=r.size;*offset=0;return buffer;
    }
    GlPool *p=gl_upload(r,offset);return p->backing.name;
}
