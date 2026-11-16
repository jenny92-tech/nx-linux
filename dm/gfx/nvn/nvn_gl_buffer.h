// INPUT: A live GLES context, a stable buffer identity and a bounded byte range.
// OUTPUT: Lazy backing growth that preserves GPU contents and existing bindings.
// POS: Private storage module of the experimental GLES backend.
#ifndef NVN_GL_BUFFER_H
#define NVN_GL_BUFFER_H
#include <GLES3/gl32.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    GLuint name;
    uint64_t capacity, grows, copied_bytes, peak_scratch;
} NvnGlBuffer;

/* The caller owns staging until submission completion. GPU copy preserves
 * command ordering without updating a large driver store from the CPU. */
static inline GLenum nvn_gl_buffer_stage_update(const NvnGlBuffer *b,GLuint staging,
        uint64_t offset,uint64_t bytes,const void *data){
    if(!b->name||!staging||!bytes||offset>b->capacity||bytes>b->capacity-offset)return GL_INVALID_VALUE;
    glBindBuffer(GL_COPY_READ_BUFFER,staging);glBufferData(GL_COPY_READ_BUFFER,bytes,data,GL_STREAM_DRAW);
    glBindBuffer(GL_COPY_WRITE_BUFFER,b->name);glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    glCopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,0,offset,bytes);
    return glGetError();
}

/* Finish earlier consumers before writing directly into the existing store.
 * Mapping the existing store avoids driver renaming an entire large pool
 * for a small patch. Keep driver synchronization and cache maintenance enabled
 * even after the fence; Unmap publishes the modified range. */
static inline GLenum nvn_gl_buffer_mapped_update(const NvnGlBuffer *b,
        uint64_t offset,uint64_t bytes,const void *data){
    if(!b->name||!bytes||!data||offset>b->capacity||bytes>b->capacity-offset)return GL_INVALID_VALUE;
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
    if(!fence)return GL_OUT_OF_MEMORY;
    GLenum status;
    do{status=glClientWaitSync(fence,GL_SYNC_FLUSH_COMMANDS_BIT,1000000000ull);}while(status==GL_TIMEOUT_EXPIRED);
    glDeleteSync(fence);
    if(status==GL_WAIT_FAILED)return GL_INVALID_OPERATION;
    glBindBuffer(GL_COPY_WRITE_BUFFER,b->name);
    void *mapped=glMapBufferRange(GL_COPY_WRITE_BUFFER,offset,bytes,GL_MAP_WRITE_BIT);
    if(!mapped)return GL_INVALID_OPERATION;
    memcpy(mapped,data,bytes);
    if(!glUnmapBuffer(GL_COPY_WRITE_BUFFER))return GL_INVALID_OPERATION;
    return glGetError();
}

/* Keep pool-relative offsets and the GL object name stable. A replacement
 * object would leave earlier UBO/SSBO/vertex/texture bindings on stale storage.
 * Growth copies the GPU store, never a possibly stale guest CPU mirror. */
static inline GLenum nvn_gl_buffer_reserve(NvnGlBuffer *b,uint64_t required,uint64_t limit){
    if(!required||required>limit||limit>PTRDIFF_MAX)return GL_INVALID_VALUE;
    if(required<=b->capacity)return GL_NO_ERROR;
    const uint64_t granule=1024*1024;
    uint64_t next=(required+granule-1)/granule*granule;
    if(next<b->capacity*2)next=b->capacity*2;
    if(next>limit)next=limit;
    GLuint scratch=0;
    if(b->capacity){
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        glGenBuffers(1,&scratch);glBindBuffer(GL_COPY_WRITE_BUFFER,scratch);
        glBufferData(GL_COPY_WRITE_BUFFER,b->capacity,NULL,GL_STREAM_COPY);
        glBindBuffer(GL_COPY_READ_BUFFER,b->name);
        glCopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,0,0,b->capacity);
        /* Complete the old-store read before redefining its storage. Otherwise
         * a driver may retain old + scratch + new stores at the growth peak.
         * Growth is rare; the stable name and all indexed bindings stay valid. */
        glFinish();
    }else glGenBuffers(1,&b->name);
    GLenum error=glGetError();
    if(!error){
        glBindBuffer(GL_COPY_WRITE_BUFFER,b->name);
        glBufferData(GL_COPY_WRITE_BUFFER,next,NULL,GL_DYNAMIC_DRAW);
        if(scratch){
            glBindBuffer(GL_COPY_READ_BUFFER,scratch);
            glCopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,0,0,b->capacity);
        }
        error=glGetError();
    }
    if(scratch)glDeleteBuffers(1,&scratch);
    if(error)return error;
    if(b->capacity){
        b->grows++;b->copied_bytes+=b->capacity;
        if(b->capacity>b->peak_scratch)b->peak_scratch=b->capacity;
    }
    b->capacity=next;
    return GL_NO_ERROR;
}
#endif
