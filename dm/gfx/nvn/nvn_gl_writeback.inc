// INPUT: Final submission writes, live pool identities and the completion fence.
// OUTPUT: Coalesced GPU bytes published after completion through bounded staging.
// POS: Synchronous GLES writeback; counters/events retain their original order.
#define GL_DOWNLOAD_BUDGET (4ull*1024*1024)
static GlPool *gl_writeback_pool(NvnVkWritebackSpan r,uint64_t *offset){
    GlPool *p=gl_pool(r.addr,r.size,offset);
    if(!p||p->serial!=r.pool_serial||!p->backing.name)gl_stop("writeback pool lifetime",r.addr);
    return p;
}
static void gl_writeback_queue(void){
    L.download_staged=0;L.download_total=0;
    for(unsigned i=0;i<L.nwrites;i++){
        GlRange r=L.writes[i];GlPool *p=gl_pool(r.addr,r.size,NULL);
        if(!p||!p->backing.name)gl_stop("writeback backing",r.addr);
        L.writeback_plan[i]=(NvnVkWritebackSpan){r.addr,r.size,p->serial};
    }
    L.writeback_count=nvn_vk_writeback_plan(L.writeback_plan,L.nwrites);
    if(L.writeback_count==SIZE_MAX)gl_stop("writeback plan",L.nwrites);
    for(size_t i=0;i<L.writeback_count;i++){
        uint64_t size=L.writeback_plan[i].size;
        if(size>UINT64_MAX-L.download_total)gl_stop("writeback total",size);
        L.download_total+=size;
    }
    /* Large loading transfers retain the direct path, keeping staging bounded.
     * The previous commit has completed before this buffer can be reused. */
    if(!L.download_total||L.download_total>GL_DOWNLOAD_BUDGET)return;
    if(!L.download_buffer)glGenBuffers(1,&L.download_buffer);
    glBindBuffer(GL_COPY_WRITE_BUFFER,L.download_buffer);
    glBufferData(GL_COPY_WRITE_BUFFER,L.download_total,NULL,GL_STREAM_READ);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    uint64_t cursor=0;
    for(size_t i=0;i<L.writeback_count;i++){
        NvnVkWritebackSpan r=L.writeback_plan[i];uint64_t offset;
        GlPool *p=gl_writeback_pool(r,&offset);
        glBindBuffer(GL_COPY_READ_BUFFER,p->backing.name);
        glCopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,offset,cursor,r.size);cursor+=r.size;
    }
    gl_check("queue writeback staging");L.download_staged=1;L.download_calls++;L.download_bytes+=L.download_total;
}
/* The caller waits for the fence AFTER queueing these copies, before either
 * reading staging or announcing counter/event/submission completion. */
static void gl_writeback_publish(void){
    const uint8_t *staged=NULL;
    if(L.download_staged){
        glBindBuffer(GL_COPY_READ_BUFFER,L.download_buffer);
        staged=glMapBufferRange(GL_COPY_READ_BUFFER,0,L.download_total,GL_MAP_READ_BIT);
        if(!staged)gl_stop("staged writeback map",L.download_total);L.download_maps++;
    }
    uint64_t cursor=0;
    for(size_t i=0;i<L.writeback_count;i++){
        NvnVkWritebackSpan r=L.writeback_plan[i];const void *bytes;
        if(staged)bytes=staged+cursor;
        else{
            uint64_t offset;GlPool *p=gl_writeback_pool(r,&offset);
            glBindBuffer(GL_COPY_READ_BUFFER,p->backing.name);
            bytes=glMapBufferRange(GL_COPY_READ_BUFFER,offset,r.size,GL_MAP_READ_BIT);
            if(!bytes)gl_stop("direct writeback map",r.addr);L.direct_readback_maps++;
        }
        dm_guest_write_prepare((void *)(uintptr_t)r.addr,r.size);
        memcpy((void *)(uintptr_t)r.addr,bytes,r.size);L.writeback_bytes+=r.size;cursor+=r.size;
        if(!staged&&!glUnmapBuffer(GL_COPY_READ_BUFFER))gl_stop("direct writeback corrupted",r.addr);
    }
    if(staged&&!glUnmapBuffer(GL_COPY_READ_BUFFER))gl_stop("staged writeback corrupted",L.download_total);
}
