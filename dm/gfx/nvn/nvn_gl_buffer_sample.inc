// INPUT: Typed read-only NVN buffer samplers and authoritative backing storage.
// OUTPUT: Direct fragment/compute SSBO reads; no buffer-to-texture snapshots.
// POS: Runtime format and address adapter for preprocessed buffer fetches.
#include "nvn_gl_buffer_sample.h"
static int gl_bind_buffer_sampler(GlShader *shader,const GlBinding *b,GLuint program){
    unsigned stage=shader->h.stage;
    if(shader->h.version!=8||(stage!=1&&stage!=5)||b->type!=7||b->slot<0||b->slot>=128||b->unit<0||b->unit>=L.max_ssbo)gl_stop("direct buffer sampler metadata",b->slot);
    unsigned id=L.state.tex[stage][b->slot]&0xfffff;
    if(id>=L.nslot||!L.slots[id].live)return gl_filter("null_direct_buffer_sampler",id);
    GlTextureSlot *slot=&L.slots[id];const tex_t *source=&slot->source;
    if(source->d.target!=NVN_TEXTURE_TARGET_BUFFER||!gl_view_supported(slot->has_view?&slot->view:NULL,source))return gl_filter("direct_buffer_sampler_view",source->serial);
    GLenum internal,format,type;unsigned bpp;GLint layout[3];
    if((source->d.format==NVN_FORMAT_R16&&!L.norm16)||!gl_format(source->d.format,&internal,&format,&type,&bpp)||!gl_buffer_format(internal)||!nvn_gl_sampler_layout(format,type,bpp,b->flags,layout)||!source->L.size||source->L.size%bpp||source->L.size/bpp>INT32_MAX)return gl_filter("direct_buffer_sampler_format",source->d.format);
    GlRange range={(uint64_t)(uintptr_t)source->mem,source->L.size};uint64_t offset;GLuint buffer;
    if(!(range.size%4)&&range.size<=65536)buffer=gl_read_buffer(range,&offset);
    else{GlPool *pool=gl_upload(range,&offset);buffer=pool->backing.name;}
    uint64_t base=offset/L.ssbo_align*L.ssbo_align,bytes=(offset-base+range.size+3)&~3ull;
    GlPool *pool=gl_pool(range.addr,range.size,NULL);
    if(range.size%4||(buffer==pool->backing.name&&base+bytes>pool->backing.capacity)){
        GLuint padded=gl_temp((range.size+3)&~3ull,NULL);glBindBuffer(GL_COPY_READ_BUFFER,buffer);glBindBuffer(GL_COPY_WRITE_BUFFER,padded);
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);glCopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,offset,0,range.size);
        buffer=padded;base=offset=0;bytes=(range.size+3)&~3ull;
    }
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER,b->unit,buffer,base,bytes);
    char uniform[64];snprintf(uniform,sizeof uniform,"dm_BufferLayout_%d",b->binding);GLint location=glGetUniformLocation(program,uniform);
    if(location>=0)glProgramUniform4i(program,location,(GLint)(offset-base),layout[0],layout[1],layout[2]);
    snprintf(uniform,sizeof uniform,"dm_BufferTexels_%d",b->binding);location=glGetUniformLocation(program,uniform);
    if(location>=0)glProgramUniform1i(program,location,(GLint)(range.size/bpp));
    L.direct_sampler_calls++;L.direct_sampler_bytes+=range.size;return 1;
}
