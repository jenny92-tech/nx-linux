// INPUT: One private Main diagnostic frame with already-bound read-only mesh shaders.
// OUTPUT: Raster/vertex probes and one private capture of nonfinite fragment values.
// POS: Bounded GLES diagnostics; probes cannot write color, depth or guest buffers.
static void gl_diag_mesh(GlShader *vs,GlShader *fs,GlGraphics *main,
        const p_draw *d,const p_draw_idx *idx,int instances,int base_instance){
    unsigned bit=!strcmp(fs->key,"e95d61a4f83affe7")?1:!strcmp(fs->key,"316637a45d955037")?2:!strcmp(fs->key,"b74497069933eb3e")?4:0;
    if(!bit||(L.diag_meshes&bit))return;L.diag_meshes|=bit;
    int primitive=d?d->prim:idx->prim,count=d?d->count:idx->count;if(primitive<4||primitive>6||count<3)return;
    /* These exact prepared surface programs have no image/storage writes.
     * Disabling all attachment writes makes the extra tests observational. */
    for(unsigned i=0;i<vs->h.nb;i++)if(vs->b[i].type==7&&vs->b[i].kind!=16&&vs->b[i].flags)return;
    for(unsigned i=0;i<fs->h.nb;i++)if(fs->b[i].type==3||fs->b[i].type==5||(fs->b[i].type==7&&fs->b[i].kind!=16&&fs->b[i].flags))return;
    uint64_t index_offset=0;GLuint index_buffer=0;GLenum index_type=0;
    if(idx){index_buffer=gl_read_buffer((GlRange){idx->ibuf,(uint64_t)idx->count*(1u<<idx->itype)},&index_offset);index_type=idx->itype==0?GL_UNSIGNED_BYTE:idx->itype==1?GL_UNSIGNED_SHORT:GL_UNSIGNED_INT;glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,index_buffer);}
    GLuint query;glGenQueries(1,&query);GLuint passed[3]={0};
    for(unsigned test=0;test<3;test++){
        gl_render_state();for(int i=0;i<L.state.rt.n;i++)glColorMaski(i,0,0,0,0);glDepthMask(GL_FALSE);glStencilMaskSeparate(GL_FRONT,0);glStencilMaskSeparate(GL_BACK,0);
        if(test!=1)glDisable(GL_CULL_FACE);if(test)glDisable(GL_DEPTH_TEST);if(test==2){glDisable(GL_STENCIL_TEST);glDisable(GL_SCISSOR_TEST);}
        glBeginQuery(GL_ANY_SAMPLES_PASSED,query);
        if(d)glDrawArraysInstanced(primitive,d->first,d->count,instances>0?instances:1);
        else glDrawElementsInstancedBaseVertex(primitive,idx->count,index_type,(void *)(uintptr_t)index_offset,idx->instances>0?idx->instances:1,idx->base_vertex);
        glEndQuery(GL_ANY_SAMPLES_PASSED);glGetQueryObjectuiv(query,GL_QUERY_RESULT,&passed[test]);
    }
    glDeleteQueries(1,&query);gl_render_state();
    fprintf(stderr,"[nvn-gles] MAIN_RASTER_PROBE vs=%s fs=%s no_cull=%u no_depth=%u no_raster_tests=%u viewport=%g,%g,%g,%g scissor=%d,%d,%u,%u stencil=%u error=%u\n",vs->key,fs->key,passed[0],passed[1],passed[2],L.state.viewport[0],L.state.viewport[1],L.state.viewport[2],L.state.viewport[3],L.state.sc.x,L.state.sc.y,L.state.sc.w,L.state.sc.h,L.state.ds.st,glGetError());
    GLuint program=glCreateProgram();glAttachShader(program,vs->object);glAttachShader(program,fs->object);const char *varying="gl_Position";glTransformFeedbackVaryings(program,1,&varying,GL_INTERLEAVED_ATTRIBS);glLinkProgram(program);GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok){char log[2048]={0};glGetProgramInfoLog(program,sizeof log,NULL,log);fprintf(stderr,"[nvn-gles] MAIN_VERTEX_PROBE_REJECT vs=%s log=%s\n",vs->key,log);glDeleteProgram(program);return;}
    if(!gl_bind_shader(vs,program)){glDeleteProgram(program);return;}
    const char *names[]={"dm_BaseVertex","dm_BaseInstance","dm_DrawID","dm_OriginY","dm_DepthZero"};const GLint values[]={d?d->first:idx->base_vertex,0,0};
    for(unsigned i=0;i<5;i++){GLint location=glGetUniformLocation(program,names[i]);if(location>=0){if(i<3)glProgramUniform1i(program,location,values[i]);else if(i==3)glProgramUniform1f(program,location,L.origin==NVN_WINDOW_ORIGIN_MODE_UPPER_LEFT?-1.0f:1.0f);else glProgramUniform1i(program,location,L.depth_mode);}}
    GLuint feedback;glGenBuffers(1,&feedback);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,feedback);glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,48,NULL,GL_STREAM_READ);glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,feedback);
    glUseProgram(program);glEnable(GL_RASTERIZER_DISCARD);glBeginTransformFeedback(GL_TRIANGLES);
    if(d)glDrawArraysInstanced(primitive,d->first,3,1);else glDrawElementsInstancedBaseVertex(primitive,3,index_type,(void *)(uintptr_t)index_offset,1,idx->base_vertex);
    glEndTransformFeedback();glDisable(GL_RASTERIZER_DISCARD);glMemoryBarrier(GL_TRANSFORM_FEEDBACK_BARRIER_BIT|GL_BUFFER_UPDATE_BARRIER_BIT);
    const float *positions=glMapBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER,0,48,GL_MAP_READ_BIT);
    fprintf(stderr,"[nvn-gles] MAIN_VERTEX_CLIP vs=%s fs=%s base_instance=%d values=",vs->key,fs->key,base_instance);
    if(positions){for(unsigned i=0;i<12;i++)fprintf(stderr,"%s%g",i?",":"",positions[i]);glUnmapBuffer(GL_TRANSFORM_FEEDBACK_BUFFER);}else fprintf(stderr,"unavailable");
    fprintf(stderr," error=%u\n",glGetError());glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,0);glDeleteBuffers(1,&feedback);glUseProgram(main->program);glDeleteProgram(program);gl_render_state();
}
static void gl_diag_fragment(GlShader *vs,GlShader *fs,GlGraphics *main,
        const p_draw *d,const p_draw_idx *idx,int instances){
    static int attempted;const char *root=getenv("DM_GLES_DIAG_FRAGMENT_ROOT");
    if(attempted||!root||!*root)return;
    if(L.max_ssbo<=31)return;
    for(unsigned i=0;i<vs->h.nb;i++)if(vs->b[i].type==7&&vs->b[i].kind!=16&&vs->b[i].flags)return;
    for(unsigned i=0;i<fs->h.nb;i++)if(fs->b[i].type==3||fs->b[i].type==5||(fs->b[i].type==7&&fs->b[i].kind!=16&&fs->b[i].flags))return;
    for(unsigned i=0;i<vs->h.nb;i++)if(vs->b[i].type==7&&vs->b[i].unit==31)return;
    for(unsigned i=0;i<fs->h.nb;i++)if(fs->b[i].type==7&&fs->b[i].unit==31)return;
    char path[1400];if(snprintf(path,sizeof path,"%s/%s.glsl",root,fs->key)>=(int)sizeof path)return;
    FILE *file=fopen(path,"rb");if(!file)return;attempted=1;
    if(fseek(file,0,SEEK_END)){fclose(file);return;}long size=ftell(file);
    if(size<=0||size>1024*1024||fseek(file,0,SEEK_SET)){fclose(file);return;}
    char *code=calloc(1,(size_t)size+1);if(!code){fclose(file);return;}
    if(fread(code,1,size,file)!=(size_t)size){free(code);fclose(file);return;}fclose(file);
    for(unsigned i=0;i<fs->h.nb;i++){const GlBinding *b=&fs->b[i];if(b->kind!=4||b->slot<0||b->slot>=128)continue;
        uint64_t handle=L.state.tex[1][b->slot];unsigned id=handle&0xfffff;if(id>=L.nslot||!L.slots[id].live)continue;
        GlTextureSlot *slot=&L.slots[id];GlTexture *t=gl_texture(&slot->source,0);if(!t)continue;
        uint64_t sampler=b->sampler>=0?L.state.samp[1][b->sampler]:handle;unsigned sid=(sampler>>20)&0xfff;GLint compare=0,min_filter=0;
        if(sid<GL_OBJECTS&&L.samplers[sid]){glGetSamplerParameteriv(L.samplers[sid],GL_TEXTURE_COMPARE_MODE,&compare);glGetSamplerParameteriv(L.samplers[sid],GL_TEXTURE_MIN_FILTER,&min_filter);}
        fprintf(stderr,"[nvn-gles] MAIN_TEXTURE unit=%d slot=%d descriptor=%u serial=%llu nvn_format=%u internal=%u extent=%u,%u,%u levels=%u gpu_owned=%d cpu_stamp=%llu cpu_dirty=%d sampler=%u compare=%u min_filter=%u view_levels=%u,%d,%d bytes=",b->unit,b->slot,id,(unsigned long long)slot->source.serial,slot->source.d.format,t->internal,slot->source.d.width,slot->source.d.height,slot->source.d.depth,slot->source.d.levels,t->gpu_owned,(unsigned long long)t->stamp,t->stamp?nvn_dirty_since(slot->source.mem,slot->source.L.size,t->stamp):0,sid,compare,min_filter,slot->has_view&&slot->view.set_levels,slot->view.base_level,slot->view.nlevels);
        if(slot->source.mem)for(unsigned j=0;j<16&&j<slot->source.L.size;j++)fprintf(stderr,"%02x",slot->source.mem[j]);fprintf(stderr,"\n");}
    GLuint shader=glCreateShader(GL_FRAGMENT_SHADER);const char *source=code;glShaderSource(shader,1,&source,NULL);glCompileShader(shader);free(code);
    GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);GLuint program=0;
    if(ok){program=glCreateProgram();glAttachShader(program,vs->object);glAttachShader(program,shader);glLinkProgram(program);glGetProgramiv(program,GL_LINK_STATUS,&ok);}
    if(!ok){char log[2048]={0};if(program)glGetProgramInfoLog(program,sizeof log,NULL,log);else glGetShaderInfoLog(shader,sizeof log,NULL,log);
        fprintf(stderr,"[nvn-gles] MAIN_FRAGMENT_REJECT key=%s log=%s\n",fs->key,log);glDeleteProgram(program);glDeleteShader(shader);return;}
    if(!gl_bind_shader(vs,program)||!gl_bind_shader(fs,program)||!gl_bind_vertices(vs)){glDeleteProgram(program);glDeleteShader(shader);return;}
    const char *names[]={"dm_BaseVertex","dm_BaseInstance","dm_DrawID","dm_OriginY","dm_DepthZero"};const GLint values[]={d?d->first:idx->base_vertex,0,0};
    for(unsigned i=0;i<5;i++){GLint location=glGetUniformLocation(program,names[i]);if(location>=0){if(i<3)glProgramUniform1i(program,location,values[i]);else if(i==3)glProgramUniform1f(program,location,L.origin==NVN_WINDOW_ORIGIN_MODE_UPPER_LEFT?-1.0f:1.0f);else glProgramUniform1i(program,location,L.depth_mode);}}
    /* Only this private SSBO is writable. Replaying the original mesh cannot
     * alter any color/depth/stencil attachment or any guest-owned buffer. */
    uint32_t initial[8192]={0};GLuint buffer;glGenBuffers(1,&buffer);glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof initial,initial,GL_STREAM_READ);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,31,buffer);
    glUseProgram(program);gl_render_state();for(int i=0;i<L.state.rt.n;i++)glColorMaski(i,0,0,0,0);glDepthMask(GL_FALSE);glStencilMaskSeparate(GL_FRONT,0);glStencilMaskSeparate(GL_BACK,0);
    /* A newly linked diagnostic VS may round depth differently from the
     * depth prepass. Observe the same mesh without rejecting equal-Z samples. */
    glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);
    if(d)glDrawArraysInstanced(d->prim,d->first,d->count,instances>0?instances:1);
    else{uint64_t offset;GLuint index_buffer=gl_read_buffer((GlRange){idx->ibuf,(uint64_t)idx->count*(1u<<idx->itype)},&offset);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,index_buffer);
        glDrawElementsInstancedBaseVertex(idx->prim,idx->count,idx->itype==0?GL_UNSIGNED_BYTE:idx->itype==1?GL_UNSIGNED_SHORT:GL_UNSIGNED_INT,(void *)(uintptr_t)offset,idx->instances>0?idx->instances:1,idx->base_vertex);}
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_BUFFER_UPDATE_BARRIER_BIT);glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);
    const uint32_t *words=glMapBufferRange(GL_SHADER_STORAGE_BUFFER,0,sizeof initial,GL_MAP_READ_BIT);
    for(unsigned slot=0;slot<2;slot++){const uint32_t *record=words?words+slot*4096:NULL;
        fprintf(stderr,"[nvn-gles] MAIN_FRAGMENT_VALUES key=%s slot=%u captured=%u bits=",fs->key,slot,record?record[0]:0);
        if(record){unsigned count=record[0]&&record[7]<4096-8?record[7]+8:8;for(unsigned i=0;i<count;i++)fprintf(stderr,"%s%08x",i?",":"",record[i]);}
        fprintf(stderr," error=%u\n",glGetError());}
    if(words)glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,31,0);glDeleteBuffers(1,&buffer);glUseProgram(main->program);glDeleteProgram(program);glDeleteShader(shader);gl_render_state();
}
