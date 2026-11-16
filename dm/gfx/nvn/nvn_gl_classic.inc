// INPUT: Immutable submitted commands and original, unlowered NVN shader metadata.
// OUTPUT: Proven counter liveness and optional classic compositor dependency plans.
// POS: CPU analysis for GLES; all rendering and completion still use the GLES backend.
static int gl_classic_shader(void *unused,const prog_t *p,unsigned stage,NvnClassicShader *out){
    (void)unused;memset(out,0,sizeof *out);if(!p)return stage==1;
    GlProgram *program=gl_program(p,stage);if(!program||!program->key[0])return 0;
    GlFacts *cached=NULL;for(unsigned i=0;i<L.nfacts;i++)if(!strcmp(L.facts[i].key,program->key)){cached=&L.facts[i];break;}
    if(!cached){
        const char *root=getenv("DM_GLES_SEMANTICS_ROOT");if(!root||!*root||L.nfacts==GL_OBJECTS)return 0;
        char path[1400];if(snprintf(path,sizeof path,"%s/%s.glfacts",root,program->key)>=(int)sizeof path)return 0;
        FILE *file=fopen(path,"rb");if(!file)return 0;GlHeader h;
        if(fread(&h,1,sizeof h,file)!=sizeof h||h.magic!=0x4d4b5644||h.version!=1||h.stage!=stage||h.nb>GL_BINDINGS||h.na>32||h.ncap>256||h.c1n>65536){fclose(file);return 0;}
        GlBinding bindings[GL_BINDINGS];uint32_t attrs[32][2];
        int valid=fread(bindings,32,h.nb,file)==h.nb&&fread(attrs,8,h.na,file)==h.na;
        if(fseek(file,0,SEEK_END)||ftell(file)!=(long)(48+h.nb*32+h.na*8+h.ncap*4+h.c1n))valid=0;fclose(file);if(!valid)return 0;
        NvnClassicShader facts={.present=1};
        for(unsigned i=0;i<h.nb;i++){
            const GlBinding *b=&bindings[i];if(b->count!=1||b->kind<1||b->kind>11||b->flags<0)return 0;
            if(((b->kind==3||b->kind==9)&&b->flags)||b->kind==6||b->type==5)facts.effects=1;
            if(b->kind==2||b->kind==3||b->kind==9){
                if(b->slot<0||b->slot>=16)return 0;unsigned bank=b->kind==3;
                facts.buffer_use[bank]|=1u<<b->slot;if(b->flags&&(b->kind==3||b->kind==9))facts.buffer_write[bank]|=1u<<b->slot;
            }
            if(b->kind==4||b->kind==6||b->kind==10||b->kind==11){
                unsigned bank=b->kind==6||b->kind==11||b->type==5;
                if(facts.nread==64||b->slot<0||b->slot>=(bank?32:128))return 0;
                unsigned at=facts.nread++;facts.read[at].bank=bank;facts.read[at].slot=b->slot;facts.read[at].write=b->kind==6||b->type==5;
            }
        }
        if(!stage)for(unsigned i=0;i<h.na;i++){if(attrs[i][0]>=16)return 0;facts.vertex_locations|=1u<<attrs[i][0];}
        cached=&L.facts[L.nfacts++];strcpy(cached->key,program->key);cached->facts=facts;cached->facts.key=cached->key;
    }
    *out=cached->facts;if(p->nstage>6)return 0;
    for(unsigned i=0;i<p->nstage;i++){
        const uint8_t *control=p->sd[i].control;if(!control)return 0;if(gl_u32(control+0x714)!=stage)continue;
        unsigned bytes=gl_u32(control+0x704);if(!bytes||bytes>16*1024*1024||out->code_size)return 0;
        out->code_address=p->sd[i].data;out->code_size=bytes;out->control_address=(uint64_t)(uintptr_t)control;
    }
    return out->code_size!=0;
}
static const tex_t *gl_classic_texture(void *unused,uint64_t handle){
    (void)unused;unsigned id=handle&0xfffff;return id<L.nslot&&L.slots[id].live?&L.slots[id].source:NULL;
}
static int gl_classic_blocks(void *unused,int format,int *bw,int *bh){(void)unused;return nvn_fmt_bpb(format,bw,bh);}
static void gl_plan_walk(const uint8_t *p,unsigned depth){
    if(depth>32){L.plan_valid=0;return;}
    for(unsigned guard=0;guard<1000000&&L.plan_valid;guard++){
        if(!gl_pool((uint64_t)(uintptr_t)p,8,NULL)){L.plan_valid=0;return;}op_hdr h;memcpy(&h,p,8);
        if(h.size<8||h.size%8||!gl_pool((uint64_t)(uintptr_t)p,h.size,NULL)){L.plan_valid=0;return;}
        if(L.plan_count==L.plan_capacity){
            size_t capacity=L.plan_capacity?L.plan_capacity*2:4096;if(capacity>262144){L.plan_valid=0;return;}
            const uint8_t **ops=realloc(L.plan_ops,capacity*sizeof *ops);if(!ops){L.plan_valid=0;return;}L.plan_ops=ops;L.plan_capacity=capacity;
        }
        L.plan_ops[L.plan_count++]=p;if(h.op==OP_END)return;
        if(h.op==OP_CALL||h.op==OP_JUMP){
            if(h.size<16){L.plan_valid=0;return;}const uint8_t *next;memcpy(&next,p+8,8);
            if(h.op==OP_JUMP){p=next;continue;}gl_plan_walk(next,depth+1);
        }
        p+=h.size;
    }
    L.plan_valid=0;
}
void mtl_sub_index(const uint64_t *handles,int n){
    const char *counter=getenv("DM_COUNTER_LIVENESS"),*classic=getenv("DM_CLASSIC_RENDER_CULL");
    L.counter_enabled=counter&&atoi(counter);L.classic_enabled=classic&&atoi(classic)&&getenv("DM_GLES_SEMANTICS_ROOT");
    const char *menu=getenv("DM_LINUX_STATIC_MENU");L.menu_enabled=menu&&!strcmp(menu,"1")&&getenv("DM_GLES_SEMANTICS_ROOT");
    L.plan_count=L.plan_position=0;L.plan_omitted=L.classic_valid=L.menu_valid=0;L.draw_counter_live=1;L.plan_valid=L.counter_enabled||L.classic_enabled||L.menu_enabled;
    if(n<0){L.plan_valid=0;return;}for(int i=0;i<n&&L.plan_valid;i++)gl_plan_walk((const uint8_t *)(uintptr_t)handles[i],0);
    if(L.plan_valid){
        uint8_t *live=realloc(L.counter_live,L.plan_count?L.plan_count:1);if(live)L.counter_live=live;else L.plan_valid=0;
        if(L.plan_valid&&!nvn_counter_plan(L.plan_ops,L.plan_count,L.counter_live))L.plan_valid=0;
    }
    if(!L.plan_valid)L.plan_fallbacks++;
    int context=dm_linux_menu_scene_context();
    if(L.classic_enabled&&L.plan_valid&&!nvn_menu_scene_skips_world_plan(context)){
        uint8_t *omit=realloc(L.classic_omit,L.plan_count?L.plan_count:1);if(omit){
            L.classic_omit=omit;NvnClassicState initial={.rt=L.state.rt,.copy_stride=L.state.copy_stride,.copy_image_stride=L.state.copy_image_stride};
            memcpy(initial.program,L.state.program,sizeof initial.program);memcpy(initial.texture,L.state.tex,sizeof initial.texture);memcpy(initial.image,L.state.img,sizeof initial.image);
            memcpy(initial.buffer[0],L.state.ub,sizeof initial.buffer[0]);memcpy(initial.buffer[1],L.state.ss,sizeof initial.buffer[1]);
            memcpy(initial.vertex,L.state.vb,sizeof initial.vertex);memcpy(initial.attribute,L.state.attr,sizeof initial.attribute);
            memset(&L.classic_memo,0,sizeof L.classic_memo);L.classic_memo.code_checks_enabled=L.classic_memo.read_checks_enabled=1;
            size_t stopped=SIZE_MAX;const char *queries=getenv("DM_CLASSIC_HD_TERRAIN_FREEZE"),*effects=getenv("DM_CLASSIC_HD_EFFECT_FREEZE");
            NvnClassicReader reader={NULL,gl_classic_shader,gl_classic_texture,&stopped,gl_classic_blocks,queries&&atoi(queries),&L.classic_memo,effects&&atoi(effects)};
            int result=nvn_classic_plan(L.plan_ops,L.plan_count,L.counter_live,initial,reader,&L.classic_history,L.classic_omit);
            L.classic_valid=result==NVN_CLASSIC_OK;
            static unsigned reasons[9];if(result>=0&&result<9&&reasons[result]++<3)fprintf(stderr,"[nvn-gles] CLASSIC_PLAN result=%d ops=%zu stopped=%zu shader=%s admitted=%d\n",result,L.plan_count,stopped,L.classic_memo.last_key?L.classic_memo.last_key:"none",L.classic_valid);
        }
    }
    if(L.menu_enabled){
        int ready=L.plan_valid&&!L.classic_valid&&!L.diag_main&&L.diag_present!=L.presents+1&&
            (context==2||context==4||context==5||(context==3&&L.menu_cache.signature));
        nvn_menu_scene_context(&L.menu_cache,ready,context,L.menu_previous,atomic_load(&L.menu_input_epoch),&L.menu_epoch);L.menu_previous=context;
        if(ready){uint8_t *omit=realloc(L.menu_omit,L.plan_count?L.plan_count:1);if(omit){
            L.menu_omit=omit;NvnClassicState initial={.rt=L.state.rt,.copy_stride=L.state.copy_stride,.copy_image_stride=L.state.copy_image_stride};
            memcpy(initial.program,L.state.program,sizeof initial.program);memcpy(initial.texture,L.state.tex,sizeof initial.texture);memcpy(initial.image,L.state.img,sizeof initial.image);
            memcpy(initial.buffer[0],L.state.ub,sizeof initial.buffer[0]);memcpy(initial.buffer[1],L.state.ss,sizeof initial.buffer[1]);
            memcpy(initial.vertex,L.state.vb,sizeof initial.vertex);memcpy(initial.attribute,L.state.attr,sizeof initial.attribute);
            memset(&L.classic_memo,0,sizeof L.classic_memo);size_t stopped=SIZE_MAX;
            NvnClassicReader reader={NULL,gl_classic_shader,gl_classic_texture,&stopped,gl_classic_blocks,0,&L.classic_memo,0};
            int result=nvn_menu_scene_plan(L.plan_ops,L.plan_count,L.counter_live,initial,reader,L.presents,&L.menu_cache,L.menu_omit);
            if(result==NVN_MENU_REUSE){L.menu_valid=1;L.menu_reuses++;}else if(result==NVN_MENU_WARM)L.menu_warm++;
            else{L.menu_fallbacks++;if(L.menu_fallbacks<4)fprintf(stderr,"[nvn-gles] MENU_PLAN_FALLBACK result=%d reason=%u detail=%llu shader=%s\n",result,L.menu_cache.reason,(unsigned long long)L.menu_cache.detail,L.classic_memo.last_key?L.classic_memo.last_key:"none");memset(&L.menu_cache,0,sizeof L.menu_cache);}
        }}
    }
    if(L.classic_enabled)dm_linux_native_hd_admission(L.classic_valid);
}
static unsigned gl_plan_operation(const uint8_t *p){
    L.draw_counter_live=1;if(!L.plan_valid)return 0;
    if(L.plan_position>=L.plan_count||L.plan_ops[L.plan_position]!=p){
        if(L.plan_omitted)gl_stop("planned command order changed after omissions",L.plan_position);
        L.plan_valid=L.classic_valid=L.menu_valid=0;L.plan_fallbacks++;return 0;
    }
    size_t at=L.plan_position++;L.draw_counter_live=L.counter_live[at];unsigned omit=L.classic_valid?L.classic_omit[at]:L.menu_valid?L.menu_omit[at]:0;
    if(omit){op_hdr h;memcpy(&h,p,8);
        if((omit==1&&!nvn_classic_draw(h.op))||(omit==2&&h.op!=OP_CLEAR_COLOR&&h.op!=OP_CLEAR_DS&&h.op!=OP_CLEAR_TEX)||(omit==3&&h.op!=OP_COPY_T2T)||(omit==4&&h.op!=OP_DISPATCH)||omit>4)gl_stop("classic omission opcode",h.op);
        L.plan_omitted=1;if(omit==1)L.classic_draws++;else if(omit==2)L.classic_clears++;else if(omit==3)L.classic_copies++;else L.classic_dispatches++;
    }
    return omit;
}
