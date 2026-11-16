// INPUT: Checked readonly UI quads or classic indexed strips, state and CPU snapshots.
// OUTPUT: Original ordered strips, flushed before any GPU side effect or state change.
// POS: Optional Linux replay optimization; full shader identity and fresh resources remain checked.
#ifdef DM_LINUX_HOST
typedef struct {
    uint8_t head[offsetof(ReplayState,vbuf)-offsetof(ReplayState,rt)];
    uint8_t tail[sizeof(ReplayState)-offsetof(ReplayState,color)];
    uint64_t epoch,pass,binding[2],registry,pool;
    unsigned writes,counter,origin,depth_mode;
    VShader *vs,*ps;
} QuadKey;
static struct {QuadKey key;VBuffer *buffer;unsigned n,stride,vertices,indexed;} quad;
static int ghost_cpu_read(Range r,void *out);
static int quad_allowed(void){
    return G.quad_batch_enabled&&!G.trace_frame&&!G.draw_census&&!G.diagnostic_work&&
        !G.pass_stats&&!G.disable_query_batch&&!G.diag_fixed_desc&&!G.diag_skip_draws&&
        !G.diag_skip_class&&!G.diag_shape&&!G.skip_ps[0]&&!ghost.busy&&!atomic_load(&ghost_on);
}
static void quad_key(QuadKey *key,VShader *vs,VShader *ps){
    G.profile_quad_key_copies++;
    memset(key,0,sizeof *key);
    memcpy(key->head,&G.state.rt,sizeof key->head);
    memcpy(key->tail,&G.state.color,sizeof key->tail);
    key->epoch=S.epoch;key->pass=G.pass_count;
    memcpy(key->binding,G.binding_revision,sizeof key->binding);
    key->registry=G.registry_revision;key->pool=G.pool_revision;
    key->writes=(unsigned)S.nwrites;key->counter=!G.counter_index_valid||G.counter_draw_live;
    key->origin=atomic_load(&G.window_origin);key->depth_mode=atomic_load(&G.depth_mode);
    key->vs=vs;key->ps=ps;
}
static int quad_key_matches(VShader *vs,VShader *ps){
    const QuadKey *key=&quad.key;
    /* Retain every byte of the original key. Compare immutable metadata first,
     * then the two live state spans directly: successful append no longer
     * needs to memset/copy a large temporary key twice. Resource refresh below
     * may flush a pass or change ownership, so both checks remain required. */
    int same=key->epoch==S.epoch&&key->pass==G.pass_count&&
        key->binding[0]==G.binding_revision[0]&&key->binding[1]==G.binding_revision[1]&&
        key->registry==G.registry_revision&&key->pool==G.pool_revision&&
        key->writes==(unsigned)S.nwrites&&
        key->counter==(unsigned)(!G.counter_index_valid||G.counter_draw_live)&&
        key->origin==(unsigned)atomic_load(&G.window_origin)&&key->depth_mode==(unsigned)atomic_load(&G.depth_mode)&&
        key->vs==vs&&key->ps==ps;
    if(same){
        /* The tail includes viewport/scissor. An append can bypass draw's
         * render_begin only under these exact original bounds and pass ID.
         * State changes flush with the old dynamic Vulkan state before the
         * next ordinary draw validates/reopens its own render area. */
        G.profile_quad_key_checks++;
        same=!memcmp(key->head,&G.state.rt,sizeof key->head)&&
            !memcmp(key->tail,&G.state.color,sizeof key->tail);
    }
    if(G.quad_key_verify){
        QuadKey reference;quad_key(&reference,vs,ps);
        if(same!=!memcmp(&reference,key,sizeof reference))die("quad key differential mismatch",G.draws);
        G.profile_quad_key_verified++;
    }
    return same;
}
static int quad_shape(const p_draw *a,const p_draw_idx *idx,int instances,int base_instance,VShader *vs,VShader *ps){
    unsigned stride=G.state.streams[0].stride;
    int sprite=G.sprite_batch_enabled&&stride==36&&vs&&ps&&
        !strcmp(vs->key,"8dd0fe0852b18a4e")&&!strcmp(ps->key,"d2cdc5f9e8a6f3a6")&&
        G.classic_index_valid&&G.state.rt.n==1&&G.state.rt.color[0]&&
        G.state.rt.color[0]->d.width==800&&G.state.rt.color[0]->d.height==600;
    if(idx){
        if(!G.indexed_batch_enabled||!sprite||idx->prim!=NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP||
           idx->count<3||idx->count>512||idx->instances!=1||idx->base_instance||!idx->ibuf||
           (idx->itype!=NVN_INDEX_TYPE_UNSIGNED_SHORT&&idx->itype!=NVN_INDEX_TYPE_UNSIGNED_INT))return 0;
    }else if(!a||a->prim!=NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP||a->first||a->count!=4||instances!=1||base_instance)return 0;
    if(!quad_allowed()||!vs||!ps||!vs->quad_safe||!ps->quad_safe||
       (!sprite&&(stride!=20||vs->h.na!=2))||(sprite&&vs->h.na!=3)||vs->draw_parameter_mask||
       G.state.streams[0].divisor||(!idx&&G.state.vbuf[0].size!=(uint64_t)stride*4)||
       (idx&&(!G.state.vbuf[0].size||G.state.vbuf[0].size>9216||G.state.vbuf[0].size%stride))||
       !G.state.vbuf[0].address||G.state.rt.n!=1||!G.state.rt.color[0]||G.state.rt.depth||
       G.state.ds.dt||G.state.ds.dw||G.state.ds.st||S.depth_input||S.feedback_flags||
       !S.read_snapshots_allowed||!S.in_pass)return 0;
    for(unsigned i=0;i<vs->h.na;i++){
        unsigned l=vs->attr[i][0];
        if(l>=16||G.state.attributes[l].stream||G.state.attributes[l].fmt==NVN_FORMAT_NONE)return 0;
        va_st v=G.state.attributes[l];NvnVkVertexFormat f=nvn_vk_vertex_format(v.fmt);
        if(!f.native||!f.alignment||v.offset>stride||f.bytes>stride-v.offset||stride%f.alignment||
           (G.state.vbuf[0].address+v.offset)%f.alignment)return 0;
    }
    for(int i=0;i<S.naligned;i++)if(S.aligned[i].written)return 0;
    return 1;
}
/* Keep ordinary per-draw ownership, dirty uploads and texture freshness. A
 * transfer may flush this batch; callers recheck it after these operations. */
static int quad_resources(VShader *s,int refresh){
    for(unsigned i=0;i<s->h.nb;i++){
        RBinding *b=&s->b[i];
        if(b->kind==2){
            if(b->slot<0||b->slot>=16)return 0;
            Range r=G.state.uniform[s->h.stage][b->slot];uint64_t off=0;
            if(!r.addr||!r.size)return 0;
            if(refresh)pool_read(r.addr,r.size,&off);
            else if(!pool_find(r.addr,r.size,&off))return 0;
            if(off%G.properties.limits.minUniformBufferOffsetAlignment)return 0;
        }else if(b->kind==4&&refresh){
            if(b->slot<0||b->slot>=128)return 0;
            unsigned id=(unsigned)G.state.tex[s->h.stage][b->slot]&0xfffff;
            if(id>=G.texture_slots||!G.tslot[id].live)return 0;
            VTexture *t=slot_texture(&G.tslot[id],1);
            if(sampling_copy_required(t,&G.tslot[id],s->h.stage))return 0;
        }
    }
    return 1;
}
static int quad_vertices(uint8_t out[144],int refresh){
    NVNbufferRange r=G.state.vbuf[0];uint64_t off=0;
    if(r.size>144)return 0;
    if(refresh)pool_read(r.address,r.size,&off);
    return ghost_cpu_read((Range){r.address,r.size},out);
}
static int quad_indexed_vertices(const p_draw_idx *idx,int refresh){
    uint8_t vertices[9216],indices[2048];NVNbufferRange r=G.state.vbuf[0];
    unsigned bytes=idx->itype==NVN_INDEX_TYPE_UNSIGNED_SHORT?2u:4u;
    Range index={idx->ibuf,(uint64_t)idx->count*bytes};uint64_t off=0;
    if(r.size>sizeof vertices||index.size>sizeof indices)return 0;
    if(refresh){pool_read(r.address,r.size,&off);if(!quad.n)return 0;pool_read(index.addr,index.size,&off);if(!quad.n)return 0;}
    if(!ghost_cpu_read((Range){r.address,r.size},vertices)||!ghost_cpu_read(index,indices))return 0;
    if(refresh&&(!quad.n||!quad_key_matches(quad.key.vs,quad.key.ps)))return 0;
    /* quad_shape admits indexed batches only for the 36-byte sprite layout.
     * Keep that proven width constant for each attribute copy in the packer. */
    return nvn_indexed_strip_pack(quad.buffer->map,quad.buffer->size,&quad.vertices,
        vertices,r.size,36u,indices,(unsigned)idx->count,bytes,idx->base_vertex);
}
static void quad_flush(void){
    if(!quad.n)return;
    if(quad.key.epoch!=S.epoch||!S.in_pass)die("quad batch submission/pass ownership",quad.n);
    unsigned n=quad.n;quad.n=0;
    command_bind_profile(NVN_VK_CMD_VERTEX,nvn_vk_cmd_vertex(&S.command_state,G.enable_command_cache,
        S.command,0,quad.buffer->b,0));
    vkCmdDraw(S.command,quad.indexed?quad.vertices:n*6u-2u,1,0,0);
    G.profile_quad_gpu_draws++;G.profile_quad_merges+=n-1;
    if(quad.stride==36){G.profile_sprite_gpu_draws++;G.profile_sprite_merges+=n-1;}
    if(quad.indexed){G.profile_indexed_gpu++;G.profile_indexed_merges+=n-1;}
}
static int quad_append(const p_draw *a,const p_draw_idx *idx,int instances,int base_instance,VShader *vs,VShader *ps){
    if(!quad.n||!quad_allowed()||quad.n==NVN_QUAD_MAX||quad.indexed!=(unsigned)!!idx)return 0;
    /* Caller has resolved and fully compared both live stages for this draw. */
    if(!quad_shape(a,idx,instances,base_instance,vs,ps))return 0;
    if(!quad_key_matches(vs,ps)||!quad_resources(vs,1)||!quad_resources(ps,1))return 0;
    if(idx){if(!quad_indexed_vertices(idx,1))return 0;quad.n++;G.profile_indexed_input++;}
    else {
        uint8_t vertices[144];if(!quad.n||!quad_vertices(vertices,1)||!quad.n)return 0;
        if(!quad_key_matches(vs,ps))return 0;
        if(!nvn_quad_pack_stride(quad.buffer->map,quad.buffer->size,&quad.n,vertices,quad.stride))return 0;
    }
    G.profile_quad_input++;G.draws++;
    if(quad.stride==36)G.profile_sprite_input++;
    if(!quad.key.counter){G.profile_query_omitted_draws++;G.counter_plan_omitted=1;}
    return 1;
}
static int quad_start(const p_draw *a,const p_draw_idx *idx,int instances,int base_instance,
        VShader *vs,VShader *ps,const PsoKey *pipeline){
    if(!quad_shape(a,idx,instances,base_instance,vs,ps)||pipeline->vertex_split||pipeline->vertex_unsigned||
       pipeline->vertex_signed||pipeline->null_bindings[0]||pipeline->null_bindings[1])return 0;
    /* Full preparation has already established freshness. Do not introduce
     * another transfer after render_begin or the active sample-query start. */
    if(!quad_resources(vs,0)||!quad_resources(ps,0))return 0;
    uint8_t vertices[144];if(!idx&&!quad_vertices(vertices,0))return 0;
    /* Keep 4 KiB groups for small UI quads. Bounded indexed terrain strips use
     * 32 KiB; capacity exhaustion flushes without partially appending a draw. */
    quad.buffer=temporary(idx?32768:4096,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    quad_key(&quad.key,vs,ps);quad.n=0;quad.stride=G.state.streams[0].stride;quad.indexed=!!idx;quad.vertices=0;
    if(idx){if(!quad_indexed_vertices(idx,0))return 0;quad.n=1;G.profile_indexed_input++;}
    else if(!nvn_quad_pack_stride(quad.buffer->map,quad.buffer->size,&quad.n,vertices,quad.stride))die("quad batch capacity",quad.buffer->size);
    if(quad.stride==36)G.profile_sprite_input++;
    G.profile_quad_input++;return 1;
}
#else
static void quad_flush(void){}
#endif
