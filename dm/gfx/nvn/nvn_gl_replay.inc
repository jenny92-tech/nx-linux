// INPUT: Bounds-checked NVN records and immutable state snapshots.
// OUTPUT: GLES draws/compute/copies, or individually counted feature exclusions.
// POS: Command interpreter of the optional GLES verification backend.
static GLenum gl_blend(unsigned f){switch(f){case 1:return GL_ZERO;case 2:return GL_ONE;case 3:return GL_SRC_COLOR;case 4:return GL_ONE_MINUS_SRC_COLOR;case 5:return GL_SRC_ALPHA;case 6:return GL_ONE_MINUS_SRC_ALPHA;case 7:return GL_DST_ALPHA;case 8:return GL_ONE_MINUS_DST_ALPHA;case 9:return GL_DST_COLOR;case 10:return GL_ONE_MINUS_DST_COLOR;case 11:return GL_SRC_ALPHA_SATURATE;case 0x61:return GL_CONSTANT_COLOR;case 0x62:return GL_ONE_MINUS_CONSTANT_COLOR;case 0x63:return GL_CONSTANT_ALPHA;case 0x64:return GL_ONE_MINUS_CONSTANT_ALPHA;default:gl_filter("dual_source_blend",f);return GL_ONE;}}
static GLenum gl_equation(unsigned e){const GLenum t[]={GL_FUNC_ADD,GL_FUNC_SUBTRACT,GL_FUNC_REVERSE_SUBTRACT,GL_MIN,GL_MAX};return e>=1&&e<=5?t[e-1]:GL_FUNC_ADD;}
static GLenum gl_stencil(unsigned e){switch(e){case NVN_STENCIL_OP_KEEP:return GL_KEEP;case NVN_STENCIL_OP_ZERO:return GL_ZERO;case NVN_STENCIL_OP_REPLACE:return GL_REPLACE;case NVN_STENCIL_OP_INCR:return GL_INCR;case NVN_STENCIL_OP_DECR:return GL_DECR;case NVN_STENCIL_OP_INVERT:return GL_INVERT;case NVN_STENCIL_OP_INCR_WRAP:return GL_INCR_WRAP;case NVN_STENCIL_OP_DECR_WRAP:return GL_DECR_WRAP;default:gl_stop("stencil operation",e);}}
static int gl_vertex_format(unsigned f,int *components,GLenum *type,int *normalized,int *integer){
    *normalized=0;*integer=0;
    if(f>=NVN_FORMAT_R8&&f<=NVN_FORMAT_RGBA32I){
        static const GLenum types[]={GL_UNSIGNED_BYTE,GL_BYTE,GL_UNSIGNED_BYTE,GL_BYTE,GL_HALF_FLOAT,GL_UNSIGNED_SHORT,GL_SHORT,GL_UNSIGNED_SHORT,GL_SHORT,GL_FLOAT,GL_UNSIGNED_INT,GL_INT};
        unsigned field=(f-1)%12;*components=(f-1)/12+1;*type=types[field];*normalized=field==0||field==1||field==5||field==6;*integer=field==2||field==3||field==7||field==8||field==10||field==11;return 1;
    }
    if(f>=NVN_FORMAT_R8_UI2F&&f<=NVN_FORMAT_RGBA32_I2F){
        static const GLenum types[]={GL_UNSIGNED_BYTE,GL_BYTE,GL_UNSIGNED_SHORT,GL_SHORT,GL_UNSIGNED_INT,GL_INT};
        unsigned field=f-NVN_FORMAT_R8_UI2F;*components=field/6+1;*type=types[field%6];return 1;
    }
    if(f==NVN_FORMAT_RGB10A2||f==NVN_FORMAT_RGB10A2_UI2F||f==NVN_FORMAT_RGB10A2SN||f==NVN_FORMAT_RGB10A2_I2F){
        *components=4;*type=f==NVN_FORMAT_RGB10A2SN||f==NVN_FORMAT_RGB10A2_I2F?GL_INT_2_10_10_10_REV:GL_UNSIGNED_INT_2_10_10_10_REV;
        *normalized=f==NVN_FORMAT_RGB10A2||f==NVN_FORMAT_RGB10A2SN;return 1;
    }
    return gl_filter("vertex_format",f);
}
static int gl_bind_vertices(GlShader *vs){
    glBindVertexArray(L.vao);for(unsigned i=0;i<16;i++)glDisableVertexAttribArray(i);
    for(unsigned i=0;i<vs->h.na;i++){
        unsigned location=vs->attrs[i][0];if(location>=16)return gl_filter("vertex_attribute_location",location);va_st a=L.state.attr[location];
        if(!a.fmt){glVertexAttrib4f(location,0,0,0,1);continue;}if(a.stream>=16)gl_stop("vertex stream",a.stream);
        NVNbufferRange r=L.state.vb[a.stream];if(!r.address||!r.size||a.offset>=r.size)return gl_filter("unbound_vertex_stream",a.stream);
        uint64_t offset;GLuint buffer=gl_read_buffer((GlRange){r.address,r.size},&offset);int count=0,norm=0,integer=0;GLenum type=0;
        if(!gl_vertex_format(a.fmt,&count,&type,&norm,&integer))return 0;
        glBindBuffer(GL_ARRAY_BUFFER,buffer);glEnableVertexAttribArray(location);const void *pointer=(void *)(uintptr_t)(offset+a.offset);
        if(integer)glVertexAttribIPointer(location,count,type,L.state.stream[a.stream].stride,pointer);
        else glVertexAttribPointer(location,count,type,norm,L.state.stream[a.stream].stride,pointer);
        glVertexAttribDivisor(location,L.state.stream[a.stream].divisor);
    }
    return 1;
}
static void gl_apply_scissor(void){
    if(L.state.sc_set){glEnable(GL_SCISSOR_TEST);glScissor(L.state.sc.x,L.state.sc.y,L.state.sc.w,L.state.sc.h);}else glDisable(GL_SCISSOR_TEST);
}
static void gl_render_state(void){
    glViewport(L.state.viewport[0],L.state.viewport[1],L.state.viewport[2],L.state.viewport[3]);glDepthRangef(L.state.near,L.state.far);gl_apply_scissor();
    if(L.state.ds.dt)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);glDepthMask(L.state.ds.dw);glDepthFunc(L.state.ds.df?GL_NEVER+L.state.ds.df-1:GL_ALWAYS);
    if(L.state.poly.cull){glEnable(GL_CULL_FACE);glCullFace(L.state.poly.cull==1?GL_FRONT:L.state.poly.cull==2?GL_BACK:GL_FRONT_AND_BACK);}else glDisable(GL_CULL_FACE);
    glFrontFace((L.state.poly.front==NVN_FRONT_FACE_CCW)^(L.origin==NVN_WINDOW_ORIGIN_MODE_UPPER_LEFT)?GL_CCW:GL_CW);
    if(L.state.ds.st){glEnable(GL_STENCIL_TEST);for(unsigned face=0;face<2;face++){GLenum f=face?GL_BACK:GL_FRONT;
        glStencilFuncSeparate(f,GL_NEVER+(face?L.state.ds.sf_b:L.state.ds.sf_f)-1,L.state.stencil_ref[face],L.state.stencil_compare[face]);glStencilMaskSeparate(f,L.state.stencil_write[face]);
        glStencilOpSeparate(f,gl_stencil(face?L.state.ds.fail_b:L.state.ds.fail_f),gl_stencil(face?L.state.ds.zfail_b:L.state.ds.zfail_f),gl_stencil(face?L.state.ds.zpass_b:L.state.ds.zpass_f));}}
    else glDisable(GL_STENCIL_TEST);
    glBlendColor(L.state.blend_color[0],L.state.blend_color[1],L.state.blend_color[2],L.state.blend_color[3]);
    for(int i=0;i<L.state.rt.n;i++){blend_st b=L.state.blend[i];if(L.state.color.blend_en&(1u<<i)){glEnablei(GL_BLEND,i);glBlendFuncSeparatei(i,gl_blend(b.src_rgb),gl_blend(b.dst_rgb),gl_blend(b.src_a),gl_blend(b.dst_a));glBlendEquationSeparatei(i,gl_equation(b.eq_rgb),gl_equation(b.eq_a));}else glDisablei(GL_BLEND,i);
        uint32_t mask=L.state.mask.mask>>(4*i);glColorMaski(i,mask&1,mask&2,mask&4,mask&8);}
    if(L.state.poly.offset_en){glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(L.state.offset.factor,L.state.offset.units);}else glDisable(GL_POLYGON_OFFSET_FILL);
}
#include "nvn_gl_diag.inc"
static int gl_shader_readonly(const GlShader *s){
    for(unsigned i=0;i<s->h.nb;i++){
        const GlBinding *b=&s->b[i];
        if(((b->kind==2||b->kind==3||b->kind==9)&&b->flags)||b->kind==6||b->kind==11||b->kind==15||(b->kind==10&&b->type==5))return 0;
        if(b->kind<1||b->kind>16||b->kind==5)return 0;
    }
    return 1;
}
static void gl_draw(const p_draw *d,const p_draw_idx *idx,int instances,int base_instance){
    int count=d?d->count:idx->count,instance_count=d?instances:idx->instances;
    if(count<0||instance_count<0)gl_stop("negative draw count",count);if(!count||!instance_count)return;
    int prim=d?d->prim:idx->prim;if(prim<0||prim>6){L.skipped_draws++;gl_filter("primitive",prim);return;}
    for(unsigned stage=2;stage<=4;stage++){const prog_t *p=L.state.program[stage];if(p)for(unsigned i=0;i<p->nstage;i++)if(p->sd[i].control&&gl_u32((const uint8_t *)p->sd[i].control+0x714)==stage){L.skipped_draws++;gl_filter("geometry_or_tessellation_stage",stage);return;}}
    if(L.state.color.logic_op!=NVN_LOGIC_OP_COPY){L.skipped_draws++;gl_filter("color_logic_operation",L.state.color.logic_op);return;}
    if(base_instance||(idx&&idx->base_instance)){L.skipped_draws++;gl_filter("base_instance",base_instance);return;}
    GlShader *vs=gl_stage(0),*fs=gl_stage(1);
    if(!L.presents&&L.draws+L.skipped_draws<128){const tex_t *rt=L.state.rt.n?L.state.rt.color[0]:NULL;
        fprintf(stderr,"[nvn-gles] DRAW_TRACE draw=%llu vs=%s fs=%s target=%llu format=%u size=%ux%u count=%d\n",
            (unsigned long long)(L.draws+L.skipped_draws),vs?vs->key:"missing",fs?fs->key:"missing",(unsigned long long)(rt?rt->serial:0),rt?rt->d.format:0,rt?rt->d.width:0,rt?rt->d.height:0,count);}
    if(!vs||!fs){L.skipped_draws++;gl_filter("missing_graphics_stage",(!vs?1:0)|(!fs?2:0));return;}
    if(vs->bad||fs->bad){L.skipped_draws++;return;}
    L.readonly_draw=gl_shader_readonly(vs)&&gl_shader_readonly(fs);
    if(!L.readonly_draw)gl_uniform_flush_all();
    if(L.mali_g52_g13&&(vs->buffer_sampler||fs->buffer_sampler)){L.skipped_draws++;gl_filter("mali_g52_g13_buffer_texture_observed_fault",vs->buffer_sampler?0:1);return;}
    GlGraphics *program=gl_graphics(vs,fs);
    if(program->bad||!gl_target()||!gl_bind_shader(vs,program->program)||!gl_bind_shader(fs,program->program)||!gl_bind_vertices(vs)){L.skipped_draws++;return;}
    const GLint values[]={d?d->first:idx->base_vertex,0,0};for(unsigned i=0;i<3;i++)if(program->uniforms[i]>=0)glProgramUniform1i(program->program,program->uniforms[i],values[i]);
    if(program->uniforms[3]>=0)glProgramUniform1f(program->program,program->uniforms[3],L.origin==NVN_WINDOW_ORIGIN_MODE_UPPER_LEFT?-1.0f:1.0f);if(program->uniforms[4]>=0)glProgramUniform1i(program->program,program->uniforms[4],L.depth_mode);
    glBindProgramPipeline(0);glUseProgram(program->program);gl_render_state();
    if(L.trace_pixels&&!L.presents&&!L.draws&&!L.skipped_draws){
        for(unsigned i=0;i<vs->h.na;i++){
            unsigned loc=vs->attrs[i][0];va_st a=L.state.attr[loc];NVNbufferRange r=L.state.vb[a.stream];GLint buffer=0;void *pointer=NULL;glGetVertexAttribiv(loc,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&buffer);glGetVertexAttribPointerv(loc,GL_VERTEX_ATTRIB_ARRAY_POINTER,&pointer);
            fprintf(stderr,"[nvn-gles] VERTEX_BYTES location=%u format=%u stream=%u stride=%u attr_offset=%u range_size=%llu bytes=",loc,a.fmt,a.stream,L.state.stream[a.stream].stride,a.offset,(unsigned long long)r.size);
            if(buffer&&r.size&&(uintptr_t)pointer>=a.offset){size_t n=r.size<128?r.size:128;glBindBuffer(GL_COPY_READ_BUFFER,buffer);const uint8_t *map=glMapBufferRange(GL_COPY_READ_BUFFER,(uintptr_t)pointer-a.offset,n,GL_MAP_READ_BIT);if(map){for(size_t j=0;j<n;j++)fprintf(stderr,"%02x",map[j]);glUnmapBuffer(GL_COPY_READ_BUFFER);}}
            fprintf(stderr,"\n");
        }
        GlRange r=L.state.ub[0][0];GLint buffer=0;GLint64 off=0,size=0;
        for(unsigned i=0;i<vs->h.nb;i++){const GlBinding *b=&vs->b[i];if(b->type==6&&(b->kind==2||b->kind==9)&&!b->slot){glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING,b->unit,&buffer);glGetInteger64i_v(GL_UNIFORM_BUFFER_START,b->unit,&off);glGetInteger64i_v(GL_UNIFORM_BUFFER_SIZE,b->unit,&size);break;}}
        fprintf(stderr,"[nvn-gles] VERTEX_MATRIX range_size=%llu values=",(unsigned long long)r.size);
        if(buffer&&size>=64){glBindBuffer(GL_COPY_READ_BUFFER,buffer);const float *map=glMapBufferRange(GL_COPY_READ_BUFFER,off,64,GL_MAP_READ_BIT);if(map){for(unsigned j=0;j<16;j++)fprintf(stderr,"%s%g",j?",":"",map[j]);glUnmapBuffer(GL_COPY_READ_BUFFER);}}
        fprintf(stderr,"\n");
    }
    if(L.diag_main==1&&L.presents>100&&L.nshader>=55&&dm_linux_menu_scene_context()==2){L.diag_main=2;L.diag_arm=L.presents;}
    if(L.diag_main==2&&L.presents>L.diag_arm){L.diag_main=0;L.diag_present=L.presents+1;fprintf(stderr,"[nvn-gles] MAIN_DIAGNOSTIC present=%llu shaders=%u one_frame=1\n",(unsigned long long)L.diag_present,L.nshader);}
    if(L.draw_counter_live||L.diag_present==L.presents+1||L.trace_pixels){if(!L.sample_query)glGenQueries(1,&L.sample_query);glBeginQuery(GL_ANY_SAMPLES_PASSED,L.sample_query);L.query_active=1;}
    else{L.counter_omitted++;L.plan_omitted=1;}
    if(d){if(instances<=0)instances=1;gl_shader_write_barrier();glDrawArraysInstanced(prim,d->first,d->count,instances);}
    else {if(idx->itype<0||idx->itype>2||idx->count<0)gl_stop("index format/count",idx->itype);unsigned bpp=1u<<idx->itype;uint64_t offset;
        GLuint buffer=gl_read_buffer((GlRange){idx->ibuf,(uint64_t)idx->count*bpp},&offset);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffer);
        gl_shader_write_barrier();glDrawElementsInstancedBaseVertex(prim,idx->count,idx->itype==0?GL_UNSIGNED_BYTE:idx->itype==1?GL_UNSIGNED_SHORT:GL_UNSIGNED_INT,(void *)(uintptr_t)offset,idx->instances>0?idx->instances:1,idx->base_vertex);}
    gl_queries_end();
    if(L.diag_present==L.presents+1){
        GLuint passed=0;glGetQueryObjectuiv(L.queries[L.nquery-1],GL_QUERY_RESULT,&passed);const tex_t *t=L.state.rt.n?L.state.rt.color[0]:NULL;
        fprintf(stderr,"[nvn-gles] MAIN_DRAW vs=%s fs=%s samples=%u target=%llu format=%u size=%ux%u count=%d origin=%d depth_mode=%d cull=%u front=%u depth=%u,%u,%u near=%g far=%g\n",vs->key,fs->key,passed,(unsigned long long)(t?t->serial:0),t?t->d.format:0,t?t->d.width:0,t?t->d.height:0,count,L.origin,L.depth_mode,L.state.poly.cull,L.state.poly.front,L.state.ds.dt,L.state.ds.dw,L.state.ds.df,L.state.near,L.state.far);
        gl_diag_mesh(vs,fs,program,d,idx,instances,base_instance);
    }
    if(L.trace_pixels&&!L.presents&&(L.draws+L.skipped_draws<4||L.draws+L.skipped_draws==52)){
        GLuint passed=0;glGetQueryObjectuiv(L.queries[L.nquery-1],GL_QUERY_RESULT,&passed);
        fprintf(stderr,"[nvn-gles] DRAW_SAMPLES draw=%llu passed=%u origin=%d depth_mode=%d viewport=%g,%g,%g,%g scissor=%d,%d,%u,%u scissor_set=%d cull=%u front=%u depth=%u,%u,%u mask=%#x blend=%#x\n",
            (unsigned long long)(L.draws+L.skipped_draws),passed,L.origin,L.depth_mode,L.state.viewport[0],L.state.viewport[1],L.state.viewport[2],L.state.viewport[3],
            L.state.sc.x,L.state.sc.y,L.state.sc.w,L.state.sc.h,L.state.sc_set,L.state.poly.cull,L.state.poly.front,L.state.ds.dt,L.state.ds.dw,L.state.ds.df,L.state.mask.mask,L.state.color.blend_en);
    }
    gl_draw_memory_barrier();GLenum error=glGetError();if(error){gl_filter("draw_driver_reject",error);L.skipped_draws++;}else L.draws++;
    if(!error&&L.classic_valid&&L.state.rt.n==1&&L.state.rt.color[0]&&L.state.rt.color[0]->d.width==800&&L.state.rt.color[0]->d.height==600&&!strcmp(fs->key,"d2cdc5f9e8a6f3a6"))L.classic_world_frame=1;
    for(int i=0;i<L.state.rt.n;i++){GlTexture *t=gl_texture(L.state.rt.color[i],0);if(t){t->gpu_owned=1;t->version++;}
        if(!i&&gl_trace_pixels(t,fs->key,L.draws+L.skipped_draws-1)&&L.diag_present==L.presents+1)gl_diag_fragment(vs,fs,program,d,idx,instances);}
    GlTexture *depth=gl_texture(L.state.rt.depth,0);if(depth){depth->gpu_owned=1;depth->version++;}
}
static void gl_dispatch(const p_dispatch *d){
    L.readonly_draw=0;gl_uniform_flush_all();
    GlShader *cs=gl_stage(5);memcpy(L.state.dispatch,&d->x,12);
    if(!cs){L.skipped_dispatches++;gl_filter("missing_compute_stage",0);return;}
    /* C146: samplerBuffer compute 1f2f8f466c29ac30 and graphics
     * 16c8f73389c7befe fault in libmali.so + 0x4183f0. Gate the observed
     * feature path only on this driver, counting every excluded operation. */
    if(L.mali_g52_g13&&cs->buffer_sampler){L.skipped_dispatches++;gl_filter("mali_g52_g13_buffer_texture_observed_fault",5);return;}
    if(cs->bad||!gl_bind_shader(cs,cs->program)){L.skipped_dispatches++;return;}
    const int logical[3]={d->x,d->y,d->z};GLuint groups[3];for(unsigned i=0;i<3;i++){
        uint64_t count=(uint64_t)(unsigned)logical[i]<<((cs->h.reserved>>(i*5))&31);
        if(logical[i]<1||count>(uint64_t)L.max_groups[i]){L.skipped_dispatches++;gl_filter("compute_dispatch_limit",count);return;}groups[i]=count;
    }
    if(L.dispatches<16)fprintf(stderr,"[nvn-gles] COMPUTE_DISPATCH key=%s groups=%d,%d,%d native_groups=%u,%u,%u program=monolithic\n",cs->key,d->x,d->y,d->z,groups[0],groups[1],groups[2]);
    gl_queries_end();glUseProgram(cs->program);gl_shader_write_barrier();glDispatchCompute(groups[0],groups[1],groups[2]);glMemoryBarrier(GL_ALL_BARRIER_BITS);glUseProgram(0);
    GLenum error=glGetError();if(error){gl_filter("compute_driver_reject",error);L.skipped_dispatches++;}else L.dispatches++;
}
static void gl_zcull_data(const p_zcull *p,int restore){
    if(p->address%32||p->size<sizeof(NvnVkZCullRecord)||!gl_pool(p->address,p->size,NULL))gl_stop("ZCull storage bounds/alignment",p->address);
    GlTexture *depth=gl_texture(L.state.rt.depth,0);if(!depth||!gl_is_depth(depth))gl_stop("ZCull without depth attachment",restore);
    const view_t *v=L.state.rt.has_dview?&L.state.rt.dview:NULL;
    unsigned level=v&&v->set_levels?v->base_level:0,layer=v&&v->set_layers?v->min_layer:0,layers=v&&v->set_layers?(unsigned)v->nlayers:depth->source.L.layers;
    if(level>=depth->source.d.levels||layer>=depth->source.L.layers||!layers||layers>depth->source.L.layers-layer)gl_stop("ZCull view bounds",depth->source.serial);
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
    /* Like Vulkan's path, GLES keeps its own depth hierarchy. A fresh or
     * foreign NVIDIA hint is never depth/stencil pixel data. Conservatively
     * discard the external hint while retaining the authoritative image. */
    if(!restore){
        NvnVkZCullRecord record=nvn_vk_zcull_record(depth->source.d.format,gl_dim(depth->source.d.width,level),gl_dim(depth->source.d.height,level),1,level,layers);
        uint64_t offset=0;GlPool *pool=gl_pool(p->address,sizeof record,&offset);if(!pool)gl_stop("ZCull save storage",p->address);gl_backing(pool,offset+sizeof record);
        gl_pool_update(pool,offset,sizeof record,&record);gl_mark((GlRange){p->address,sizeof record});
    }
}
static void gl_copy_texture(const p_copy_t2t *p){
    if(p->flags&~(NVN_COPY_FLAGS_ENGINE_2D_BIT|NVN_COPY_FLAGS_LINEAR_FILTER_BIT))gl_stop("texture copy flags",p->flags);
    uint32_t decoded;int decoded_bpp,bc_destination=nvn_bc_unpacked(p->dst->d.format,&decoded,&decoded_bpp);
    GlTexture *s=gl_texture(p->src,1),*d=gl_texture(p->dst,bc_destination);if(!s||!d){gl_filter("texture_copy_resource",0);return;}
    if(d->unorm16_float&&!s->unorm16_float)gl_stop("R16 GPU conversion requires native norm16",p->dst->serial);
    int astc_decode=((p->src->d.format==NVN_FORMAT_RGBA_ASTC_4x4&&d->internal==GL_RGBA8)||
        (p->src->d.format==NVN_FORMAT_RGBA_ASTC_4x4_SRGB&&d->internal==GL_SRGB8_ALPHA8))&&
        s->target==GL_TEXTURE_2D&&(d->target==GL_TEXTURE_2D||d->target==GL_TEXTURE_2D_ARRAY)&&s->name!=d->name&&
        p->sr.width==p->dr.width&&p->sr.height==p->dr.height&&p->sr.depth==p->dr.depth;
    if(astc_decode){
        unsigned sl=p->has_sv&&p->sv.set_levels?p->sv.base_level:0,dl=p->has_dv&&p->dv.set_levels?p->dv.base_level:0;
        unsigned layer=p->dr.zoffset+(p->has_dv&&p->dv.set_layers?p->dv.min_layer:0);
        if(p->sr.depth!=1||p->sr.zoffset||p->sr.xoffset<0||p->sr.yoffset<0||p->dr.xoffset<0||p->dr.yoffset<0||p->sr.width<1||p->sr.height<1||
            sl>=p->src->d.levels||dl>=p->dst->d.levels||layer>=p->dst->L.layers||
            (uint64_t)p->sr.xoffset+p->sr.width>gl_dim(p->src->d.width,sl)||(uint64_t)p->sr.yoffset+p->sr.height>gl_dim(p->src->d.height,sl)||
            (uint64_t)p->dr.xoffset+p->dr.width>gl_dim(p->dst->d.width,dl)||(uint64_t)p->dr.yoffset+p->dr.height>gl_dim(p->dst->d.height,dl)){
            gl_filter("ASTC decoded copy extent",p->dst->serial);return;
        }
        gl_queries_end();glBindFramebuffer(GL_FRAMEBUFFER,L.fbo);
        for(unsigned i=1;i<8&&i<(unsigned)L.max_color;i++)gl_attach(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0+i,NULL,NULL);
        gl_attach(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,NULL,NULL);gl_attach(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,NULL,NULL);
        view_t view={0};view.set_levels=view.set_layers=1;view.base_level=dl;view.min_layer=layer;
        gl_attach(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,d,&view);GLenum buffer=GL_COLOR_ATTACHMENT0;glDrawBuffers(1,&buffer);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){gl_filter("ASTC decoded copy framebuffer",d->source.serial);return;}
        GLenum error=nvn_gl_astc_copy_draw(&L.astc_copy,s->name,L.max_tex-1,sl,p->sr.xoffset,p->sr.yoffset,p->dr.xoffset,p->dr.yoffset,p->dr.width,p->dr.height);
        if(error)gl_filter("ASTC decoded copy draw",error);
        else{d->gpu_owned=1;d->version++;L.astc_copies++;L.astc_texels+=(uint64_t)p->dr.width*p->dr.height;}
        return;
    }
    if(p->sr.width==p->dr.width&&p->sr.height==p->dr.height&&p->sr.depth==p->dr.depth&&s->internal==d->internal){
        unsigned sl=p->has_sv&&p->sv.set_levels?p->sv.base_level:0,dl=p->has_dv&&p->dv.set_levels?p->dv.base_level:0;
        int sz=p->sr.zoffset+(p->has_sv&&p->sv.set_layers?p->sv.min_layer:0),dz=p->dr.zoffset+(p->has_dv&&p->dv.set_layers?p->dv.min_layer:0);
        glCopyImageSubData(s->name,s->target,sl,p->sr.xoffset,p->sr.yoffset,sz,d->name,d->target,dl,p->dr.xoffset,p->dr.yoffset,dz,p->sr.width,p->sr.height,p->sr.depth);
        GLenum e=glGetError();if(e)gl_filter("image_copy_driver_reject",e);else{d->gpu_owned=1;d->version++;}return;
    }
    gl_queries_end();glBindFramebuffer(GL_READ_FRAMEBUFFER,L.read_fbo);gl_attach(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,s,p->has_sv?&p->sv:NULL);glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,L.fbo);for(unsigned i=1;i<8&&i<(unsigned)L.max_color;i++)gl_attach(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0+i,NULL,NULL);gl_attach(GL_DRAW_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,NULL,NULL);gl_attach(GL_DRAW_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,NULL,NULL);gl_attach(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,d,p->has_dv?&p->dv:NULL);GLenum buffer=GL_COLOR_ATTACHMENT0;glDrawBuffers(1,&buffer);glDisable(GL_SCISSOR_TEST);
    if(p->sr.depth!=1||p->dr.depth!=1||gl_is_depth(s)||gl_is_depth(d)||glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE||glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){
        static unsigned reported;if(reported++<8)fprintf(stderr,"[nvn-gles] COPY_SHAPE_REJECT src=%llu fmt=%u target=%u size=%d,%d,%d dst=%llu fmt=%u target=%u size=%d,%d,%d\n",(unsigned long long)s->source.serial,s->source.d.format,s->target,p->sr.width,p->sr.height,p->sr.depth,(unsigned long long)d->source.serial,d->source.d.format,d->target,p->dr.width,p->dr.height,p->dr.depth);
        gl_filter("texture_copy_shape",s->source.serial);return;}

    glBlitFramebuffer(p->sr.xoffset,p->sr.yoffset,p->sr.xoffset+p->sr.width,p->sr.yoffset+p->sr.height,p->dr.xoffset,p->dr.yoffset,p->dr.xoffset+p->dr.width,p->dr.yoffset+p->dr.height,GL_COLOR_BUFFER_BIT,p->flags&NVN_COPY_FLAGS_LINEAR_FILTER_BIT?GL_LINEAR:GL_NEAREST);
    GLenum error=glGetError();if(error)gl_filter("texture_copy_driver_reject",error);
    else{d->gpu_owned=1;d->version++;}
}
static void gl_copy_buffer_texture(const p_copy_b2t *p){
    uint32_t decoded;int decoded_bpp,bc=nvn_bc_unpacked(p->tex->d.format,&decoded,&decoded_bpp);
    GlTexture *t=gl_texture(p->tex,bc||(p->tex->d.format>=NVN_FORMAT_RGBA_ASTC_4x4&&p->tex->d.format<=NVN_FORMAT_RGBA_ASTC_12x12_SRGB));if(!t)return;unsigned level=p->has_view&&p->view.set_levels?p->view.base_level:0;
    /* ENGINE_2D chooses NVIDIA's engine, not a pixel transformation. */
    if((p->flags&~NVN_COPY_FLAGS_ENGINE_2D_BIT)||t->target==GL_TEXTURE_BUFFER||p->r.width<1||p->r.height<1||p->r.depth<1||p->r.xoffset<0||p->r.yoffset<0||p->r.zoffset<0){gl_filter("buffer_texture_copy_region",p->flags);return;}
    int bw,bh,block=nvn_fmt_bpb(p->tex->d.format,&bw,&bh);size_t row,image,size;uint64_t offset;
    if(!nvn_copy_pitch(p->r.width,p->r.height,p->r.depth,bw,bh,block,L.state.copy_stride,L.state.copy_image_stride,&row,&image,&size))gl_stop("buffer texture pitch",L.state.copy_stride);
    unsigned layer=p->r.zoffset+(p->has_view&&p->view.set_layers?p->view.min_layer:0),available=t->target==GL_TEXTURE_3D?gl_dim(p->tex->d.depth,level):p->tex->L.layers;
    if(level>=p->tex->d.levels||(uint64_t)p->r.xoffset+p->r.width>gl_dim(p->tex->d.width,level)||(uint64_t)p->r.yoffset+p->r.height>gl_dim(p->tex->d.height,level)||layer>=available||(unsigned)p->r.depth>available-layer)gl_stop("buffer texture extent",p->tex->serial);
    GlPool *pool=gl_upload((GlRange){p->src,size},&offset);
    if(bc||!t->bpp||t->unorm16_float){
        glBindBuffer(GL_COPY_READ_BUFFER,pool->backing.name);const uint8_t *map=glMapBufferRange(GL_COPY_READ_BUFFER,offset,size,GL_MAP_READ_BIT);if(!map)gl_stop("compressed copy source map",p->src);
        size_t columns=((size_t)p->r.width+bw-1)/bw,rows=((size_t)p->r.height+bh-1)/bh,packed_row=columns*block;
        size_t bytes=bc||t->unorm16_float?(size_t)p->r.width*p->r.height*t->bpp:packed_row*rows;uint8_t *pixels=malloc(bytes);if(!pixels)gl_stop("compressed copy allocation",bytes);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glBindTexture(t->target,t->name);glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
        for(int z=0;z<p->r.depth;z++){
            if(bc){if(nvn_bc_decode(p->tex->d.format,map+image*z,row,p->r.width,p->r.height,pixels,(size_t)p->r.width*t->bpp))gl_stop("buffer texture BC decode",p->tex->d.format);}
            else if(t->unorm16_float){for(int y=0;y<p->r.height;y++)for(int x=0;x<p->r.width;x++){uint16_t value;memcpy(&value,map+image*z+row*y+x*2,2);((float *)pixels)[(size_t)y*p->r.width+x]=(float)value/65535.0f;}}
            else for(size_t y=0;y<rows;y++)memcpy(pixels+y*packed_row,map+image*z+y*row,packed_row);
            GLenum target=t->target==GL_TEXTURE_CUBE_MAP?GL_TEXTURE_CUBE_MAP_POSITIVE_X+layer+z:t->target;
            if(bc||t->unorm16_float){
                if(gl_storage_3d(t->target))glTexSubImage3D(target,level,p->r.xoffset,p->r.yoffset,layer+z,p->r.width,p->r.height,1,t->format,t->type,pixels);
                else glTexSubImage2D(target,level,p->r.xoffset,p->r.yoffset,p->r.width,p->r.height,t->format,t->type,pixels);
            }else {
                if(gl_storage_3d(t->target))glCompressedTexSubImage3D(target,level,p->r.xoffset,p->r.yoffset,layer+z,p->r.width,p->r.height,1,t->internal,bytes,pixels);
                else glCompressedTexSubImage2D(target,level,p->r.xoffset,p->r.yoffset,p->r.width,p->r.height,t->internal,bytes,pixels);
            }
        }
        free(pixels);glBindBuffer(GL_COPY_READ_BUFFER,pool->backing.name);if(!glUnmapBuffer(GL_COPY_READ_BUFFER))gl_stop("compressed copy source unmap",p->src);
        GLenum error=glGetError();if(error)gl_filter("compressed_buffer_texture_driver_reject",error);else{t->gpu_owned=1;t->version++;}return;
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pool->backing.name);glBindTexture(t->target,t->name);glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,row/t->bpp);
    for(int z=0;z<p->r.depth;z++){const void *data=(void *)(uintptr_t)(offset+image*z);int layer=p->r.zoffset+z+(p->has_view&&p->view.set_layers?p->view.min_layer:0);
        if(gl_storage_3d(t->target))glTexSubImage3D(t->target,level,p->r.xoffset,p->r.yoffset,layer,p->r.width,p->r.height,1,t->format,t->type,data);
        else glTexSubImage2D(t->target==GL_TEXTURE_CUBE_MAP?GL_TEXTURE_CUBE_MAP_POSITIVE_X+layer:t->target,level,p->r.xoffset,p->r.yoffset,p->r.width,p->r.height,t->format,t->type,data);}
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);GLenum error=glGetError();if(error)gl_filter("buffer_texture_driver_reject",error);else{t->gpu_owned=1;t->version++;}
}
static int gl_integer_texture(const GlTexture *t){return t->format==GL_RED_INTEGER||t->format==GL_RG_INTEGER||t->format==GL_RGBA_INTEGER;}
static void gl_clear_texture(const p_clear_tex *p){
    GlTexture *t=gl_texture(p->tex,0);if(!t)return;if(t->unorm16_float)gl_stop("R16 GPU clear requires native norm16",p->tex->serial);const view_t *v=p->has_view?&p->view:NULL;
    unsigned level=v&&v->set_levels?v->base_level:0,layer=p->r.zoffset+(v&&v->set_layers?v->min_layer:0);
    int bw,bh;nvn_fmt_bpb(p->tex->d.format,&bw,&bh);
    if(t->target==GL_TEXTURE_BUFFER||bw!=1||bh!=1||gl_is_depth(t)||(v&&((v->set_format&&v->format!=p->tex->d.format)||(v->set_target&&v->target!=p->tex->d.target)))){gl_filter("texture_clear_shape_or_view",p->tex->serial);return;}
    unsigned available=t->target==GL_TEXTURE_3D?gl_dim(p->tex->d.depth,level):p->tex->L.layers;
    if(level>=p->tex->d.levels||p->r.xoffset<0||p->r.yoffset<0||p->r.zoffset<0||p->r.width<0||p->r.height<0||p->r.depth<1||
       (uint64_t)p->r.xoffset+p->r.width>gl_dim(p->tex->d.width,level)||(uint64_t)p->r.yoffset+p->r.height>gl_dim(p->tex->d.height,level)||layer>=available||(unsigned)p->r.depth>available-layer)gl_stop("texture clear extent",p->tex->serial);
    if(!p->r.width||!p->r.height)return;
    gl_queries_end();glBindFramebuffer(GL_FRAMEBUFFER,L.fbo);
    for(int i=1;i<8&&i<L.max_color;i++)gl_attach(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0+i,NULL,NULL);
    gl_attach(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,NULL,NULL);gl_attach(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,NULL,NULL);
    GLenum buffer=GL_COLOR_ATTACHMENT0;glDrawBuffers(1,&buffer);glEnable(GL_SCISSOR_TEST);glScissor(p->r.xoffset,p->r.yoffset,p->r.width,p->r.height);glColorMaski(0,p->mask&1,p->mask&2,p->mask&4,p->mask&8);
    GLfloat f[4];GLuint u[4];GLint si[4];
    for(unsigned i=0;i<4;i++){f[i]=p->is_uint?(float)p->c.u[i]:p->c.f[i];double value=f[i];if(value!=value)value=0;double uv=value,iv=value;
        if(uv<0)uv=0;if(uv>UINT32_MAX)uv=UINT32_MAX;if(iv<INT32_MIN)iv=INT32_MIN;if(iv>INT32_MAX)iv=INT32_MAX;
        u[i]=p->is_uint?p->c.u[i]:(uint32_t)uv;if(p->is_uint)memcpy(&si[i],&u[i],4);else si[i]=(GLint)iv;}
    for(unsigned z=0;z<(unsigned)p->r.depth;z++){
        view_t slice={0};slice.set_levels=slice.set_layers=1;slice.base_level=level;slice.min_layer=layer+z;
        gl_attach(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,t,&slice);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){gl_filter("texture_clear_framebuffer",p->tex->serial);return;}
        if(gl_integer_texture(t)){if(t->type==GL_INT)glClearBufferiv(GL_COLOR,0,si);else glClearBufferuiv(GL_COLOR,0,u);}else glClearBufferfv(GL_COLOR,0,f);
    }
    GLenum error=glGetError();if(error)gl_filter("texture_clear_driver_reject",error);else{t->gpu_owned=1;t->version++;}
}
static void gl_payload(const op_hdr *h,size_t n){if(h->size<8+n)gl_stop("truncated command payload",h->op);}
static void gl_execute(const uint8_t *p,unsigned depth){
    if(depth>32)gl_stop("command recursion",depth);
    for(unsigned guard=0;guard<1000000;guard++){
        if(!gl_pool((uint64_t)(uintptr_t)p,8,NULL))gl_stop("command header outside pool",(uint64_t)(uintptr_t)p);op_hdr h;memcpy(&h,p,8);
        if(h.size<8||h.size%8||!gl_pool((uint64_t)(uintptr_t)p,h.size,NULL))gl_stop("command size",h.size);const uint8_t *a=p+8;
        if(gl_plan_operation(p)){p+=h.size;continue;}
        if(!gl_uniform_opcode_keeps(h.op))gl_uniform_flush_all();
#define PAY(T) gl_payload(&h,sizeof(T));const T *v=(const void *)a
        switch(h.op){
        case OP_END:return;case OP_BEGIN:break;
        case OP_CALL:case OP_JUMP:{gl_payload(&h,8);const uint8_t *next;memcpy(&next,a,8);if(h.op==OP_JUMP){p=next;continue;}gl_execute(next,depth+1);break;}
        case OP_SIGNAL_EVENT:{gl_payload(&h,16);volatile uint32_t *addr;uint32_t mode,value;memcpy(&addr,a,8);memcpy(&mode,a+8,4);memcpy(&value,a+12,4);mtl_sub_event(addr,mode,value);break;}
        case OP_REPORT_COUNTER:{gl_payload(&h,16);uint64_t type,address;memcpy(&type,a,8);memcpy(&address,a+8,8);mtl_sub_counter(address,type);break;}
        case OP_RESET_COUNTER:gl_payload(&h,8);if(gl_u32(a)!=NVN_COUNTER_TYPE_SAMPLES_PASSED)gl_stop("counter reset type",gl_u32(a));gl_counter_reset();break;
        case OP_COPY_B2B:{gl_payload(&h,24);uint64_t src,dst,n;memcpy(&src,a,8);memcpy(&dst,a+8,8);memcpy(&n,a+16,8);mtl_sub_copy(src,dst,n);break;}
        case OP_UPDATE_UB:{gl_payload(&h,16);uint64_t dst,n,off;memcpy(&dst,a,8);memcpy(&n,a+8,8);if(n>h.size-24)gl_stop("uniform update length",n);GlPool *p=gl_pool(dst,n,&off);if(!p)gl_stop("uniform update range",dst);if(gl_uniform_defer((GlRange){dst,n},a+16))break;gl_uniform_flush_all();gl_backing(p,off+n);gl_pool_update(p,off,n,a+16);gl_mark((GlRange){dst,n});break;}
        case OP_SET_RT:gl_payload(&h,sizeof(p_set_rt));memcpy(&L.state.rt,a,sizeof L.state.rt);break;
        case OP_CLEAR_COLOR:{PAY(p_clear_color);if(v->index<0||v->index>=8)gl_stop("clear attachment",v->index);if(gl_target()){GlTexture *t=gl_texture(L.state.rt.color[v->index],0);gl_apply_scissor();glColorMaski(v->index,v->mask&1,v->mask&2,v->mask&4,v->mask&8);
            if(t&&gl_integer_texture(t)){GLuint raw[4];memcpy(raw,v->rgba,16);if(t->type==GL_INT)glClearBufferiv(GL_COLOR,v->index,(const GLint *)raw);else glClearBufferuiv(GL_COLOR,v->index,raw);}else glClearBufferfv(GL_COLOR,v->index,v->rgba);if(t){t->gpu_owned=1;t->version++;}}break;}
        case OP_CLEAR_DS:{PAY(p_clear_ds);if(gl_target()){gl_apply_scissor();glDepthMask(v->depth_mask);glStencilMask(v->stencil_mask);if(v->depth_mask&&v->stencil_mask)glClearBufferfi(GL_DEPTH_STENCIL,0,v->depth,v->stencil);else if(v->depth_mask)glClearBufferfv(GL_DEPTH,0,&v->depth);else if(v->stencil_mask)glClearBufferiv(GL_STENCIL,0,&v->stencil);GlTexture *t=gl_texture(L.state.rt.depth,0);if(t){t->gpu_owned=1;t->version++;}}break;}
        case OP_VIEWPORTS:{PAY(p_viewports);if(v->first||v->n!=1)gl_filter("multiple_viewports",v->n);memcpy(L.state.viewport,v->v[0],16);break;}
        case OP_SCISSOR:gl_payload(&h,sizeof(p_scissor));memcpy(&L.state.sc,a,sizeof L.state.sc);L.state.sc_set=1;break;
        case OP_DEPTH_RANGE:{PAY(p_depth_range);L.state.near=v->n;L.state.far=v->f;break;}
        case OP_BIND_PROGRAM:{PAY(p_bind_program);for(unsigned stage=0;stage<6;stage++)if(v->stages&(1u<<stage))L.state.program[stage]=v->prog;break;}
        case OP_BIND_STATE:{PAY(p_bind_state);switch(v->kind){case 0:{blend_st b;memcpy(&b,v->bytes,sizeof b);if(b.target>=8)gl_stop("blend target",b.target);L.state.blend[b.target]=b;break;}case 1:memcpy(&L.state.color,v->bytes,sizeof(color_st));break;case 2:memcpy(&L.state.mask,v->bytes,sizeof(chmask_st));break;case 3:memcpy(&L.state.ms,v->bytes,sizeof(ms_st));break;case 4:memcpy(&L.state.poly,v->bytes,sizeof(poly_st));break;case 5:memcpy(&L.state.ds,v->bytes,sizeof(ds_st));break;default:gl_stop("state kind",v->kind);}break;}
        case OP_BIND_VATTR:case OP_BIND_VSTREAM:case OP_BIND_VBUF:{gl_payload(&h,8);int first,n;memcpy(&first,a,4);memcpy(&n,a+4,4);if(first<0||n<0||first>16||n>16-first)gl_stop("vertex state range",n);size_t size=h.op==OP_BIND_VATTR?sizeof(va_st):h.op==OP_BIND_VSTREAM?sizeof(vs_st):sizeof(NVNbufferRange);gl_payload(&h,8+n*size);
            void *dst=h.op==OP_BIND_VATTR?(void *)(L.state.attr+first):h.op==OP_BIND_VSTREAM?(void *)(L.state.stream+first):(void *)(L.state.vb+first);memcpy(dst,a+8,n*size);break;}
        case OP_BIND_UBO:case OP_BIND_SSBO:{PAY(p_bind_buf);if(v->stage<0||v->stage>=6||v->index<0||v->index>=16)gl_stop("buffer binding range",v->index);GlRange *dst=h.op==OP_BIND_UBO?L.state.ub[v->stage]:L.state.ss[v->stage];dst[v->index]=(GlRange){v->addr,v->size};break;}
        case OP_BIND_TEX:case OP_BIND_SAMP:case OP_BIND_IMG:{gl_payload(&h,offsetof(p_bind_handles,h));const p_bind_handles *v=(const void *)a;int limit=h.op==OP_BIND_IMG?32:128;if(v->stage<0||v->stage>=6||v->first<0||v->n<0||v->n>32||v->first>limit||v->n>limit-v->first)gl_stop("texture binding range",v->n);gl_payload(&h,offsetof(p_bind_handles,h)+v->n*8);
            uint64_t *dst=h.op==OP_BIND_TEX?L.state.tex[v->stage]:h.op==OP_BIND_SAMP?L.state.samp[v->stage]:L.state.img[v->stage];memcpy(dst+v->first,v->h,v->n*8);break;}
        case OP_STENCIL:{PAY(p_stencil);for(unsigned i=0;i<2;i++)if(v->face&(1u<<i)){if(!v->which)L.state.stencil_compare[i]=v->value_mask;else if(v->which==1)L.state.stencil_write[i]=v->mask;else if(v->which==2)L.state.stencil_ref[i]=v->ref;else gl_stop("stencil state",v->which);}break;}
        case OP_POLY_OFFSET:gl_payload(&h,sizeof(p_poly_offset));memcpy(&L.state.offset,a,sizeof L.state.offset);break;
        case OP_BLEND_COLOR:gl_payload(&h,sizeof(p_blend_color));memcpy(L.state.blend_color,a,16);break;
        case OP_DRAW:{PAY(p_draw);gl_draw(v,NULL,1,0);break;}
        case OP_DRAW_IDX:{PAY(p_draw_idx);gl_draw(NULL,v,0,0);break;}
        case OP_DRAW_INSTANCED:{PAY(p_draw_instanced);gl_draw(&v->draw,NULL,v->instances,v->base_instance);break;}
        case OP_DISPATCH:{PAY(p_dispatch);gl_dispatch(v);break;}
        case OP_BARRIER:gl_payload(&h,4);if(gl_u32(a)!=NVN_BARRIER_INVALIDATE_SHADER_BIT||!L.readonly_draw)gl_uniform_flush_all();gl_queries_end();if(L.coherent_reads&&L.readonly_draw&&gl_u32(a)==NVN_BARRIER_INVALIDATE_SHADER_BIT)L.coherent_guest_barriers++;else glMemoryBarrier(GL_ALL_BARRIER_BITS);break;
        case OP_WAIT_SYNC:case OP_FENCE_SYNC:{PAY(p_sync);if(!v->sync||v->sync->magic!=MAGIC('S','Y','N','C'))gl_stop("sync object lifetime",0);glMemoryBarrier(GL_ALL_BARRIER_BITS);if(h.op==OP_FENCE_SYNC)dm_nvn_sync_publish(v->sync,L.fence+1);break;}
        case OP_WAIT_EVENT:{gl_payload(&h,16);uint64_t addr;uint32_t mode,value;memcpy(&addr,a,8);memcpy(&mode,a+8,4);memcpy(&value,a+12,4);if(!gl_pool(addr,4,NULL)||mode>1)gl_stop("event wait range",addr);mtl_sub_commit();uint64_t start=gl_now();for(;;){uint32_t n=__atomic_load_n((uint32_t *)(uintptr_t)addr,__ATOMIC_SEQ_CST);if(mode==0?n==value:(int32_t)(n-value)>=0)break;if(gl_now()-start>30000000000ull)gl_stop("event wait deadline",addr);struct timespec delay={0,100000};nanosleep(&delay,NULL);}mtl_sub_begin();break;}
        case OP_COPY_T2T:{PAY(p_copy_t2t);gl_copy_texture(v);break;}
        case OP_COPY_T2B:gl_stop("observable texture-to-buffer copy requires GLES implementation",h.op);
        case OP_COPY_B2T:{PAY(p_copy_b2t);gl_copy_buffer_texture(v);break;}
        case OP_CLEAR_TEX:{PAY(p_clear_tex);gl_clear_texture(v);break;}
        case OP_DEPTH_CLAMP:gl_payload(&h,4);if(gl_u32(a))gl_filter("depth_clamp",1);break;
        case OP_SAVE_ZCULL:case OP_RESTORE_ZCULL:{PAY(p_zcull);gl_zcull_data(v,h.op==OP_RESTORE_ZCULL);break;}
        case OP_SET_TEX_POOL:case OP_SET_SAMP_POOL:gl_payload(&h,sizeof(p_set_pool));break;
        case OP_COPY_STRIDE:case OP_COPY_IMAGE_STRIDE:{PAY(p_copy_stride);if(h.op==OP_COPY_STRIDE)L.state.copy_stride=v->stride;else L.state.copy_image_stride=v->stride;break;}
        case OP_SHADER_SCRATCH:gl_payload(&h,sizeof(p_bind_buf));break;
        case OP_DEBUG:gl_payload(&h,sizeof(p_debug));break;
        default:gl_stop("unknown command opcode",h.op);
        }
#undef PAY
        p+=h.size;
    }
    gl_stop("command iteration limit",depth);
}
void mtl_exec(const uint8_t *handle){gl_execute(handle,0);}
