// INPUT: Complete classic batches and deliberately observable HD dependencies.
// OUTPUT: Hidden work is removed only when roots, effects, aliases and counters permit it.
// POS: Host ASan/UBSan regression for classic-only quality planning.
#define DM_LINUX_HOST 1
#include "nvn_vk_classic_plan.h"
#include <assert.h>
#include <stdio.h>

static prog_t programs[4];static tex_t images[7];static int effect,unknown,root_buffer,root_vertex,light_image;
static NvnClassicState initial;
static const char *compute_key;
static int freeze_queries,freeze_effects;
static int memo_enabled,code_cache_enabled,read_cache_enabled;static uint64_t shader_control;static NvnClassicMemo memo;
static int shader(void *u,const prog_t *p,unsigned stage,NvnClassicShader *s){
    (void)u;memset(s,0,sizeof *s);if(unknown)return 0;
    s->present=1;s->key="ordinary";s->code_address=0x1000;s->code_size=16;
    s->control_address=shader_control;
    if(stage==0&&root_vertex)s->vertex_locations=1;
    if(stage==5){
        s->buffer_use[1]=1;
        if(p==&programs[3]){s->key=compute_key;s->buffer_write[1]=1;
            s->nread=1;s->read[0].slot=0;
            if(light_image){s->nread=2;s->read[1].bank=1;s->read[1].slot=1;s->read[1].write=1;}}
    }
    if(stage==1){
        if(p==&programs[2]){s->key="e44ae4b6e36e3424";s->nread=2;s->read[1].slot=1;}
        else if(p==&programs[0]){s->nread=1;if(root_buffer)s->buffer_use[0]=1;}
        else s->effects=effect;
    }
    return 1;
}
static const tex_t *texture(void *u,uint64_t handle){(void)u;return handle>0&&handle<7?&images[handle]:NULL;}
typedef struct { op_hdr h;union {p_set_rt rt;p_bind_program program;p_bind_handles textures;p_draw draw;p_clear_color clear;uint64_t words[3];p_copy_t2t copy;p_bind_buf buffer;p_dispatch dispatch;p_draw_idx index;p_copy_b2t b2t;} p; } Command;
static Command c[32];static const uint8_t *ops[32];static uint8_t live[32],omit[32];static size_t n,hidden,root_binding;
static Command *command(unsigned op,size_t size){assert(n<32);Command *p=&c[n];memset(p,0,sizeof *p);p->h=(op_hdr){op,(uint32_t)((8+size+7)&~7u)};ops[n++]=(const uint8_t *)p;return p;}
static void rt(unsigned id){command(OP_SET_RT,sizeof(p_set_rt))->p.rt=(p_set_rt){.n=1,.color={&images[id]}};}
static void program(unsigned id){command(OP_BIND_PROGRAM,sizeof(p_bind_program))->p.program=(p_bind_program){&programs[id],3};}
static void textures(unsigned a,unsigned b){p_bind_handles *p=&command(OP_BIND_TEX,offsetof(p_bind_handles,h)+16)->p.textures;*p=(p_bind_handles){.stage=1,.n=2,.h={a,b}};}
static void draw(void){command(OP_DRAW,sizeof(p_draw))->p.draw=(p_draw){.count=3};}
static void setup(void){
    n=0;effect=unknown=root_buffer=root_vertex=light_image=0;memset(live,0,sizeof live);memset(&initial,0,sizeof initial);
    compute_key="30a35d4b6d77a433";
    freeze_queries=1;freeze_effects=0;
    shader_control=0;
    for(unsigned i=1;i<7;i++)images[i]=(tex_t){.serial=i,.mem=(void *)(uintptr_t)(0x100000*i),.L={.size=4096}};
    rt(4);command(OP_CLEAR_COLOR,sizeof(p_clear_color))->p.clear=(p_clear_color){.mask=15};program(1);hidden=n;draw();
    rt(2);program(0);textures(5,0);draw();rt(3);root_binding=n;textures(5,0);draw();
    rt(1);program(2);textures(2,3);draw();live[n-1]=1;
}
static void lighting(void){
    command(OP_BIND_PROGRAM,sizeof(p_bind_program))->p.program=(p_bind_program){&programs[3],32};
    command(OP_BIND_SSBO,sizeof(p_bind_buf))->p.buffer=(p_bind_buf){.stage=5,.addr=0x800000,.size=4096};
    command(OP_BIND_TEX,offsetof(p_bind_handles,h)+8)->p.textures=(p_bind_handles){.stage=5,.n=1,.h={4}};
    if(light_image)command(OP_BIND_IMG,offsetof(p_bind_handles,h)+16)->p.textures=(p_bind_handles){.stage=5,.n=2,.h={0,6}};
    command(OP_DISPATCH,sizeof(p_dispatch))->p.dispatch=(p_dispatch){.x=1,.y=1,.z=1};
}
static int format_blocks(void *u,int format,int *bw,int *bh){(void)u;(void)format;*bw=*bh=1;return 4;}
static int plan_with_history(NvnClassicTargets *history){
    memo.n=0;memo.last_key=NULL;memset(memo.slot,0,sizeof memo.slot);
    memo.code_checks_enabled=(unsigned)code_cache_enabled;memo.code_checks=memo.code_hits=0;
    memo.read_checks_enabled=(unsigned)read_cache_enabled;memo.read_checks=memo.read_hits=0;
    return nvn_classic_plan(ops,n,live,initial,(NvnClassicReader){NULL,shader,texture,NULL,format_blocks,freeze_queries,memo_enabled?&memo:NULL,freeze_effects},history,omit);
}
static int plan(void){NvnClassicTargets history={0};return plan_with_history(&history);}
int main(void){
    for(read_cache_enabled=0;read_cache_enabled<2;read_cache_enabled++)
    for(code_cache_enabled=0;code_cache_enabled<2;code_cache_enabled++)for(memo_enabled=0;memo_enabled<2;memo_enabled++){
    setup();assert(plan()==NVN_CLASSIC_OK);assert(omit[hidden]==1&&omit[1]==2);for(size_t i=0;i<n;i++)if(i!=hidden&&i!=1)assert(!omit[i]);
    if(memo_enabled){assert(memo.code_checks>0);assert((memo.code_hits>0)==!!code_cache_enabled);}
    if(memo_enabled){assert(memo.read_checks>0);assert((memo.read_hits>0)==!!read_cache_enabled);}
    /* A later resource bind must invalidate a successful stage check while
     * preserving per-draw index/vertex hazard validation. */
    setup();root_buffer=1;lighting();rt(2);program(0);textures(5,0);
    command(OP_BIND_UBO,sizeof(p_bind_buf))->p.buffer=(p_bind_buf){.stage=1,.addr=0xb00000,.size=16};draw();
    command(OP_BIND_UBO,sizeof(p_bind_buf))->p.buffer=(p_bind_buf){.stage=1,.addr=0x800020,.size=16};draw();
    rt(1);program(2);textures(2,3);draw();assert(plan()==NVN_CLASSIC_DEPENDENCY);
    /* Reusing the same cache object must not exempt a later-batch write. A
     * write recorded after repeated draws is already known at first check. */
    setup();shader_control=0xb00000;assert(plan()==NVN_CLASSIC_OK);
    Command *late=command(OP_UPDATE_UB,24);late->p.words[0]=shader_control+0x87c;late->p.words[1]=4;
    assert(plan()==NVN_CLASSIC_EFFECT);
    setup();shader_control=0xb00000;late=command(OP_UPDATE_UB,24);late->p.words[0]=shader_control+0x880;late->p.words[1]=4;
    assert(plan()==NVN_CLASSIC_OK); /* Adjacent bytes are not overlapping. */
    setup();live[hidden]=1;assert(plan()==NVN_CLASSIC_LIVE_COUNTER);
    setup();effect=1;assert(plan()==NVN_CLASSIC_EFFECT);
    setup();c[root_binding].p.textures.h[0]=4;assert(plan()==NVN_CLASSIC_DEPENDENCY);
    setup();images[4].mem=images[2].mem;assert(plan()==NVN_CLASSIC_ALIAS);
    setup();unknown=1;assert(plan()==NVN_CLASSIC_NOT_SELECTED);
    setup();c[hidden].h.size=8;assert(plan()==NVN_CLASSIC_BAD_RECORD);
    setup();Command *p=command(OP_UPDATE_UB,24);p->p.words[0]=0x1000;p->p.words[1]=4;assert(plan()==NVN_CLASSIC_EFFECT);
    setup();p=command(OP_COPY_T2B,sizeof(p_copy_t2t));p->p.copy.src=(const tex_t *)(uintptr_t)1;assert(plan()==NVN_CLASSIC_DEPENDENCY);
    setup();p=command(OP_COPY_T2T,sizeof(p_copy_t2t));p->p.copy.src=&images[4];p->p.copy.dst=&images[5];assert(plan()==NVN_CLASSIC_DEPENDENCY);
    setup();p=command(OP_COPY_T2T,sizeof(p_copy_t2t));p->p.copy.src=&images[4];p->p.copy.dst=&images[6];assert(plan()==NVN_CLASSIC_OK&&omit[n-1]==3);
    setup();p=command(OP_COPY_T2T,sizeof(p_copy_t2t));p->p.copy.src=&images[4];p->p.copy.dst=&images[2];assert(plan()==NVN_CLASSIC_DEPENDENCY);
    setup();lighting();assert(plan()==NVN_CLASSIC_OK&&omit[n-1]==4);
    const char *known[]={"30a35d4b6d77a433","1f2f8f466c29ac30","83365fcb02fc4a9a",NVN_CLASSIC_TERRAIN_QUERY_KEY};
    for(unsigned i=0;i<4;i++){setup();lighting();compute_key=known[i];assert(plan()==NVN_CLASSIC_OK&&omit[n-1]==4);}
    const char *fluid[]={"0c7adefa3a80bbcb","eb8a0d0872a77ef4","3f27364f7a346ee6"};
    for(unsigned i=0;i<3;i++){
        setup();lighting();compute_key=fluid[i];assert(plan()==NVN_CLASSIC_DEPENDENCY);
        freeze_effects=1;assert(plan()==NVN_CLASSIC_OK&&omit[n-1]==4);
        root_buffer=1;initial.buffer[0][1][0]=(NvnClassicRange){0x800020,64};assert(plan()==NVN_CLASSIC_DEPENDENCY);
        setup();light_image=1;lighting();compute_key=fluid[i];freeze_effects=1;
        c[n-3].p.textures.h[0]=5; /* Ordinary input, not an HD attachment. */
        NvnClassicTargets history={0};
        assert(plan_with_history(&history)==NVN_CLASSIC_OK&&omit[n-1]==4);
        assert(nvn_classic_cause(&history,6)==NVN_CLASSIC_TARGET_FLUID);
        freeze_effects=0;
        assert(plan_with_history(&history)==NVN_CLASSIC_OK&&!omit[n-1]);
        c[root_binding].p.textures.h[0]=6;
        assert(plan_with_history(&history)==NVN_CLASSIC_DEPENDENCY); /* Root still guarded. */
        c[root_binding].p.textures.h[0]=5;compute_key="ordinary";
        assert(plan_with_history(&history)==NVN_CLASSIC_DEPENDENCY); /* Unknown CS still guarded. */
        compute_key=fluid[i];nvn_classic_add(&history,&images[6]);
        assert(plan_with_history(&history)==NVN_CLASSIC_DEPENDENCY); /* Mixed provenance is never exempt. */
    }
    setup();lighting();compute_key="other-HD-looking-program";assert(plan()==NVN_CLASSIC_DEPENDENCY);
    setup();lighting();root_buffer=1;initial.buffer[0][1][0]=(NvnClassicRange){0x800020,64};assert(plan()==NVN_CLASSIC_DEPENDENCY);
    setup();lighting();root_vertex=1;initial.vertex[0]=(NVNbufferRange){.address=0x800000,.size=64};assert(plan()==NVN_CLASSIC_DEPENDENCY);
    setup();lighting();command(OP_DRAW_IDX,sizeof(p_draw_idx))->p.index=(p_draw_idx){.itype=NVN_INDEX_TYPE_UNSIGNED_SHORT,.count=3,.ibuf=0x800002};assert(plan()==NVN_CLASSIC_DEPENDENCY);
    setup();lighting();images[5].mem=(void *)(uintptr_t)0x800100;assert(plan()==NVN_CLASSIC_DEPENDENCY);
    setup();light_image=1;lighting();assert(plan()==NVN_CLASSIC_OK&&omit[n-1]==4);
    setup();light_image=1;lighting();images[6].mem=images[2].mem;assert(plan()==NVN_CLASSIC_DEPENDENCY);
    setup();lighting();command(OP_BIND_PROGRAM,sizeof(p_bind_program))->p.program=(p_bind_program){&programs[1],32};
    command(OP_DISPATCH,sizeof(p_dispatch));assert(plan()==NVN_CLASSIC_DEPENDENCY);
    setup();lighting();p=command(OP_COPY_B2B,24);p->p.words[0]=0x800000;p->p.words[1]=0x900000;p->p.words[2]=16;assert(plan()==NVN_CLASSIC_DEPENDENCY);
    setup();lighting();p=command(OP_WAIT_EVENT,16);p->p.words[0]=0x800000;assert(plan()==NVN_CLASSIC_DEPENDENCY);
    setup();lighting();p=command(OP_UPDATE_UB,24);p->p.words[0]=0x1000;p->p.words[1]=4;assert(plan()==NVN_CLASSIC_EFFECT);
    setup();lighting();c[n-1].h.size=8;assert(plan()==NVN_CLASSIC_BAD_RECORD);
    setup();lighting();c[n-3].p.buffer.addr=UINT64_MAX-16;assert(plan()==NVN_CLASSIC_BAD_RECORD);
    setup();lighting();p=command(OP_COPY_B2T,sizeof(p_copy_b2t));p->p.b2t=(p_copy_b2t){.src=0xa00000,.tex=&images[5],.r={.width=4,.height=4,.depth=1}};assert(plan()==NVN_CLASSIC_OK&&!omit[n-1]);
    p->p.b2t.src=0x800100;assert(plan()==NVN_CLASSIC_DEPENDENCY);
    p->p.b2t.src=0x7fff00;initial.copy_stride=128;assert(plan()==NVN_CLASSIC_DEPENDENCY);
    initial.copy_stride=-1;assert(plan()==NVN_CLASSIC_BAD_RECORD);
    setup();lighting();compute_key=NVN_CLASSIC_TERRAIN_QUERY_KEY;freeze_queries=0;assert(plan()==NVN_CLASSIC_DEPENDENCY);
    freeze_queries=1;p=command(OP_COPY_B2B,24);p->p.words[0]=0x800000;p->p.words[1]=0xa00000;p->p.words[2]=32;
    assert(plan()==NVN_CLASSIC_OK&&!omit[n-1]);
    root_buffer=1;initial.buffer[0][1][0]=(NvnClassicRange){0xa00000,32};assert(plan()==NVN_CLASSIC_DEPENDENCY);
    setup();lighting();c[n-3].p.buffer.addr=0x1000;assert(plan()==NVN_CLASSIC_EFFECT);
    setup();images[4].mem=(void *)(uintptr_t)0x1000;assert(plan()==NVN_CLASSIC_EFFECT);
    }
    NvnClassicState state={.valid=2,.reads_valid=2};
    state.shader[1].buffer_use[0]=1;
    p_bind_buf binding={.stage=1,.index=3,.addr=0xb00000,.size=16};
    op_hdr header={OP_BIND_UBO,8+sizeof binding};
    assert(nvn_classic_state(&state,header,(const uint8_t *)&binding)&&state.reads_valid==2);
    binding.index=0;assert(nvn_classic_state(&state,header,(const uint8_t *)&binding)&&!state.reads_valid);
    state.reads_valid=2;state.shader[1].nread=1;state.shader[1].read[0].slot=2;
    struct {p_bind_handles b;uint64_t handle;} handle;memset(&handle,0,sizeof handle);
    handle.b.stage=1;handle.b.first=3;handle.b.n=1;handle.b.h[0]=5;
    header=(op_hdr){OP_BIND_TEX,8+offsetof(p_bind_handles,h)+8};
    assert(nvn_classic_state(&state,header,(const uint8_t *)&handle)&&state.reads_valid==2);
    handle.b.first=2;assert(nvn_classic_state(&state,header,(const uint8_t *)&handle)&&!state.reads_valid);
    state.reads_valid=2;state.valid=0;binding.index=3;binding.addr++;
    header=(op_hdr){OP_BIND_UBO,8+sizeof binding};
    assert(nvn_classic_state(&state,header,(const uint8_t *)&binding)&&!state.reads_valid);
    puts("classic planner: roots/counters/aliases/code/readback, used-binding invalidation and HD light output consumer guards passed");
}
