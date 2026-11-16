// INPUT: Synthetic menu/UI/scene/compositor command streams and shader facts.
// OUTPUT: Assertions for warmup, preserved UI/events and conservative rejection.
// POS: Host-only menu scene policy checks; no game assets or GPU required.
#include <assert.h>
#include <stdio.h>
#include "../../gfx/nvn/nvn_vk_menu_scene.h"
typedef struct { op_hdr h;union { p_set_rt rt;p_bind_program program;p_draw draw;
    p_clear_color clear;p_dispatch dispatch;uint64_t words[8]; } p; } Command;
static Command commands[32];static const uint8_t *ops[32];static size_t n;
static uint8_t live[32],omit[32];static tex_t images[4];static prog_t programs[4];
static size_t ui_draw,scene_draw,final_draw,scene_clear,ui_clear,compute,event;
static int unknown;
static Command *command(unsigned op,size_t bytes){
    assert(n<32);Command *c=&commands[n];memset(c,0,sizeof *c);
    c->h=(op_hdr){op,(uint32_t)((8+bytes+7)&~7u)};ops[n++]=(const uint8_t *)c;return c;
}
static void target(unsigned i){command(OP_SET_RT,sizeof(p_set_rt))->p.rt=(p_set_rt){.n=1,.color={&images[i]}};}
static void program(unsigned i,unsigned stages){command(OP_BIND_PROGRAM,sizeof(p_bind_program))->p.program=(p_bind_program){&programs[i],(int)stages};}
static void draw(void){command(OP_DRAW,sizeof(p_draw))->p.draw=(p_draw){.count=3};}
static int shader(void *user,const prog_t *p,unsigned stage,NvnClassicShader *out){
    (void)user;if(!p||unknown)return 0;memset(out,0,sizeof *out);out->present=1;
    unsigned id=(unsigned)(p-programs);
    out->key=stage==0?(id==0?"ede50e40fab8e849":"951a0dd4c4b2a230"):
        id==2?"2ad026ea00fb309e":"0123456789abcdef";
    return 1;
}
static void setup(void){
    n=0;unknown=0;memset(live,0,sizeof live);
    for(unsigned i=0;i<4;i++)images[i]=(tex_t){.serial=i+1,.d={.width=960,.height=720}};
    target(0);ui_clear=n;command(OP_CLEAR_COLOR,sizeof(p_clear_color))->p.clear=(p_clear_color){.mask=15};
    program(0,3);ui_draw=n;draw();
    target(1);scene_clear=n;command(OP_CLEAR_COLOR,sizeof(p_clear_color))->p.clear=(p_clear_color){.mask=15};
    program(1,3);scene_draw=n;draw();
    program(3,32);compute=n;command(OP_DISPATCH,sizeof(p_dispatch))->p.dispatch=(p_dispatch){.x=1,.y=1,.z=1};
    event=n;command(OP_SIGNAL_EVENT,16)->p.words[0]=0x1000;
    target(2);program(2,3);final_draw=n;draw();live[final_draw]=1;
    command(OP_END,0);
}
static int plan(uint64_t frame,NvnMenuSceneCache *cache){
    return nvn_menu_scene_plan(ops,n,live,(NvnClassicState){0},
        (NvnClassicReader){.shader=shader},frame,cache,omit);
}
int main(void){
    assert(nvn_menu_scene_skips_world_plan(2));
    assert(nvn_menu_scene_skips_world_plan(4));
    assert(nvn_menu_scene_skips_world_plan(5));
    const int conservative_contexts[]={-1,0,1,3,6};
    for(unsigned i=0;i<sizeof conservative_contexts/sizeof *conservative_contexts;i++)
        assert(!nvn_menu_scene_skips_world_plan(conservative_contexts[i]));
    setup();NvnMenuSceneCache cache={0};
    for(unsigned frame=1;frame<=NVN_MENU_WARM_FRAMES;frame++)assert(plan(frame,&cache)==NVN_MENU_WARM);
    assert(plan(NVN_MENU_WARM_FRAMES+1,&cache)==NVN_MENU_REUSE);
    assert(omit[scene_draw]==1&&omit[scene_clear]==2&&omit[compute]==4);
    assert(!omit[ui_draw]&&!omit[ui_clear]&&!omit[final_draw]&&!omit[event]);
    for(unsigned frame=NVN_MENU_WARM_FRAMES+2;frame<=NVN_MENU_SETTLE_FRAMES;frame++)assert(plan(frame,&cache)==NVN_MENU_REUSE);
    assert(plan(NVN_MENU_SETTLE_FRAMES+1,&cache)==NVN_MENU_WARM);
    assert(plan(NVN_MENU_SETTLE_FRAMES+2,&cache)==NVN_MENU_REUSE);
    /* Settings -> the same main menu redraws once to remove Settings paint;
     * opening again reuses that refreshed scene. A graphics edit still warms. */
    uint64_t epoch=9;
    nvn_menu_scene_context(&cache,1,3,2,9,&epoch);
    assert(plan(33,&cache)==NVN_MENU_REUSE);
    nvn_menu_scene_context(&cache,1,2,3,9,&epoch);
    assert(plan(34,&cache)==NVN_MENU_WARM);
    assert(plan(35,&cache)==NVN_MENU_REUSE);
    nvn_menu_scene_context(&cache,1,3,2,9,&epoch);
    assert(plan(36,&cache)==NVN_MENU_REUSE&&!omit[ui_draw]&&!omit[event]);
    images[1].serial=10;assert(plan(37,&cache)==NVN_MENU_WARM);
    for(unsigned frame=38;frame<=42;frame++)plan(frame,&cache);
    nvn_menu_scene_context(&cache,1,2,3,10,&epoch);
    assert(epoch==10&&plan(43,&cache)==NVN_MENU_WARM);
    nvn_menu_scene_context(&cache,0,1,2,10,&epoch);
    assert(!cache.signature&&!cache.stable);
    nvn_menu_scene_context(&cache,1,4,3,10,&epoch);
    assert(cache.dynamic_geometry&&plan(44,&cache)==NVN_MENU_WARM);
    for(unsigned frame=45;frame<=49;frame++)plan(frame,&cache);
    nvn_menu_scene_context(&cache,1,3,4,10,&epoch);
    plan(50,&cache);
    nvn_menu_scene_context(&cache,1,2,3,10,&epoch);
    assert(!cache.signature); // Settings from a different preview is not the same main scene.
    nvn_menu_scene_context(&cache,0,0,4,10,&epoch);
    assert(!cache.signature&&!cache.dynamic_geometry);
    setup();cache=(NvnMenuSceneCache){0};
    for(unsigned frame=1;frame<=NVN_MENU_WARM_FRAMES+1;frame++)plan(frame,&cache);
    nvn_menu_scene_context(&cache,1,3,2,10,&epoch);
    nvn_menu_scene_context(&cache,1,2,3,10,&epoch);
    assert(!cache.signature&&plan(7,&cache)==NVN_MENU_WARM); // Entrance is still settling.
    setup();cache=(NvnMenuSceneCache){0};
    for(unsigned frame=1;frame<=NVN_MENU_WARM_FRAMES+1;frame++)plan(frame,&cache);
    commands[scene_draw].p.draw.count=6;assert(plan(40,&cache)==NVN_MENU_WARM);
    for(unsigned i=0;i<100;i++)assert(plan(40,&cache)==NVN_MENU_WARM);
    assert(cache.stable==0);
    live[scene_draw]=1;assert(plan(33,&cache)==NVN_MENU_UNSELECTED);
    setup();cache=(NvnMenuSceneCache){0};
    commands[scene_draw-3].p.rt=(p_set_rt){.depth=&images[1]};
    commands[scene_clear].h.op=OP_CLEAR_DS;
    for(unsigned frame=1;frame<=NVN_MENU_WARM_FRAMES;frame++)assert(plan(frame,&cache)==NVN_MENU_WARM);
    assert(plan(NVN_MENU_WARM_FRAMES+1,&cache)==NVN_MENU_REUSE&&omit[scene_draw]==1);
    setup();cache=(NvnMenuSceneCache){0};
    commands[scene_draw-3].p.rt=(p_set_rt){.n=2,.color={&images[1],NULL}};
    for(unsigned frame=1;frame<=NVN_MENU_WARM_FRAMES;frame++)assert(plan(frame,&cache)==NVN_MENU_WARM);
    assert(plan(NVN_MENU_WARM_FRAMES+1,&cache)==NVN_MENU_REUSE);
    setup();cache=(NvnMenuSceneCache){.dynamic_geometry=1};
    for(unsigned frame=1;frame<=NVN_MENU_WARM_FRAMES+1;frame++){
        commands[scene_draw].p.draw.count=(int)frame*3;
        assert(plan(frame,&cache)==(frame<=NVN_MENU_DYNAMIC_WARM_FRAMES?NVN_MENU_WARM:NVN_MENU_REUSE));
    }
    images[1].d.width=480;assert(plan(10,&cache)==NVN_MENU_WARM);
    setup();unknown=1;assert(plan(34,&cache)==NVN_MENU_INVALID);
    setup();commands[scene_draw-3].p.rt.color[0]=&images[0];assert(plan(35,&cache)==NVN_MENU_UNSELECTED);
    setup();commands[final_draw-1].p.program.prog=&programs[1];assert(plan(36,&cache)==NVN_MENU_UNSELECTED);
    setup();commands[scene_draw].h.size=8;assert(plan(37,&cache)==NVN_MENU_INVALID);
    setup();cache=(NvnMenuSceneCache){.dynamic_geometry=1};epoch=50;
    for(unsigned f=1;f<=40;f++)plan(f,&cache);
    uint64_t saved=cache.signature;
    nvn_menu_scene_context(&cache,1,4,4,51,&epoch);
    assert(cache.signature==saved&&cache.refresh==NVN_MENU_INPUT_DELAY_FRAMES);
    for(unsigned f=100;f<103;f++){
        assert(plan(f,&cache)==NVN_MENU_REUSE);
        unsigned refresh=cache.refresh;
        for(unsigned repeat=0;repeat<5;repeat++)assert(plan(f,&cache)==NVN_MENU_REUSE&&cache.refresh==refresh);
    }
    assert(plan(103,&cache)==NVN_MENU_WARM);
    assert(plan(103,&cache)==NVN_MENU_WARM);
    for(unsigned f=104;f<180;f++)assert(plan(f,&cache)==NVN_MENU_REUSE);
    nvn_menu_scene_context(&cache,1,4,4,52,&epoch);
    assert(plan(180,&cache)==NVN_MENU_REUSE);
    unsigned pending=cache.refresh;
    nvn_menu_scene_context(&cache,1,4,4,53,&epoch);
    assert(cache.refresh==pending); // Rapid input cannot postpone a pending refresh forever.
    assert(plan(181,&cache)==NVN_MENU_REUSE);
    assert(plan(182,&cache)==NVN_MENU_REUSE);
    assert(plan(183,&cache)==NVN_MENU_WARM);
    nvn_menu_scene_context(&cache,1,4,4,54,&epoch);
    images[1].serial+=20;
    assert(plan(184,&cache)==NVN_MENU_WARM&&!cache.refresh); // New target must initialize immediately.
    nvn_menu_scene_context(&cache,1,2,4,54,&epoch);
    assert(!cache.signature&&!cache.refresh);
    setup();cache=(NvnMenuSceneCache){.dynamic_geometry=1};epoch=60;
    assert(plan(1,&cache)==NVN_MENU_WARM);
    for(unsigned f=2;f<=8;f++)assert(plan(f,&cache)==NVN_MENU_REUSE);
    nvn_menu_scene_context(&cache,1,4,4,61,&epoch);
    assert(!cache.signature&&!cache.refresh); // Streaming entrance keeps full warmup and late refresh.
    assert(plan(9,&cache)==NVN_MENU_WARM);
    for(unsigned f=10;f<=40;f++)plan(f,&cache);
    nvn_menu_scene_context(&cache,1,4,4,62,&epoch);
    assert(cache.refresh);
    nvn_menu_scene_context(&cache,0,0,4,62,&epoch);
    assert(!cache.signature&&!cache.refresh);
    /* Static creation must not perform a heavy 3D redraw or sample a partial
     * action pose when navigation, View Class or held input changes the epoch. */
    setup();cache=(NvnMenuSceneCache){.dynamic_geometry=1};epoch=70;
    for(unsigned f=1;f<=40;f++)plan(f,&cache);
    saved=cache.signature;
    for(unsigned f=41;f<=100;f++){
        nvn_menu_scene_context(&cache,1,5,5,f+70,&epoch);
        assert(cache.signature==saved&&!cache.refresh);
        assert(plan(f,&cache)==NVN_MENU_REUSE);
        assert(!omit[ui_draw]&&!omit[ui_clear]&&!omit[final_draw]&&!omit[event]);
    }
    images[1].serial+=20;assert(plan(101,&cache)==NVN_MENU_WARM);
    nvn_menu_scene_context(&cache,1,4,5,171,&epoch);
    assert(!cache.signature&&plan(102,&cache)==NVN_MENU_WARM);
    puts("menu scene cache: live UI/events, warmup, geometry changes and rejection passed");
    return 0;
}
