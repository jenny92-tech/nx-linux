// INPUT: Complete immutable menu batch and checked original shader identities.
// OUTPUT: Static creation lineup and cached previews; UI/compositor/events remain live.
// POS: Menu quality policy, never selected in gameplay or unknown compositors.
#pragma once
#include "nvn_vk_classic_plan.h"

typedef struct { uint64_t signature,frame,detail;unsigned stable,reason,dynamic_geometry,settings_origin,refresh; } NvnMenuSceneCache;
enum { NVN_MENU_UNSELECTED=0,NVN_MENU_WARM=1,NVN_MENU_REUSE=2,NVN_MENU_INVALID=3 };
enum { NVN_MENU_WARM_FRAMES=5 };
enum { NVN_MENU_DYNAMIC_WARM_FRAMES=1 };
enum { NVN_MENU_SETTLE_FRAMES=30 };
enum { NVN_MENU_INPUT_DELAY_FRAMES=4 };
/* Verified menu roots cannot select the classic world compositor. Unknown,
 * world and Settings contexts keep the complete world analysis. */
static inline int nvn_menu_scene_skips_world_plan(int context){
    return context==2||context==4||context==5;
}
/* Settings can paint into a cached scene target. Returning to the same main
 * menu must redraw it once, rather than freezing the Settings background or
 * restarting the whole entrance warmup. Select/create transitions and
 * unready contexts still reset. Settled previews retain their last image for
 * a few frames after input, allowing the guest to consume it before one redraw.
 * Creation keeps all eight standing portraits fixed: selecting/viewing a class
 * updates the live UI without sampling one frame of its action animation.
 * A pending redraw is never postponed by repeated input. */
static inline void nvn_menu_scene_context(NvnMenuSceneCache *cache,int ready,
        int context,int previous,uint64_t epoch,uint64_t *previous_epoch){
    unsigned origin=context==3?(previous==3?cache->settings_origin:(unsigned)previous):0;
    int same_main=context==2&&previous==3&&cache->settings_origin==2&&
        cache->stable>NVN_MENU_SETTLE_FRAMES;
    if(!ready||(context!=previous&&context!=3&&!same_main))memset(cache,0,sizeof *cache);
    else if(epoch!=*previous_epoch&&context!=5){
        if(context==4&&cache->signature&&cache->stable>=NVN_MENU_SETTLE_FRAMES){
            if(!cache->refresh){
                cache->refresh=NVN_MENU_INPUT_DELAY_FRAMES;
                cache->stable=NVN_MENU_SETTLE_FRAMES+1;cache->frame=UINT64_MAX;
            }
        }else memset(cache,0,sizeof *cache);
    }else if(same_main&&cache->signature){
        cache->stable=NVN_MENU_SETTLE_FRAMES-1;cache->frame=UINT64_MAX;
    }
    *previous_epoch=epoch;
    cache->dynamic_geometry=context==4||context==5;
    cache->settings_origin=origin;
}
static inline uint64_t nvn_menu_mix(uint64_t h,uint64_t v){return (h^v)*1099511628211ull;}
static inline uint64_t nvn_menu_key(uint64_t h,const char *key){
    if(key)for(unsigned i=0;i<16&&key[i];i++)h=nvn_menu_mix(h,(unsigned char)key[i]);
    return h;
}
static inline int nvn_menu_ui(const NvnClassicState *s){
    return s->shader[0].present&&s->shader[0].key&&!strcmp(s->shader[0].key,"ede50e40fab8e849");
}
static inline int nvn_menu_final(const NvnClassicState *s){
    return s->shader[1].present&&s->shader[1].key&&!strcmp(s->shader[1].key,"2ad026ea00fb309e");
}
/* Warm actual frames after entering/changing the scene. Every batch still
 * validates its shape and shader identities. This deliberately freezes 3D
 * animation/exposure in the main menu; it is not API-equivalent draw culling.
 * No reports, events, CPU copies or readbacks are omitted. */
static inline int nvn_menu_scene_plan(const uint8_t *const *ops,size_t count,
        const uint8_t *counter_live,NvnClassicState initial,NvnClassicReader r,
        uint64_t frame,NvnMenuSceneCache *cache,uint8_t *omit){
    if(!count||!counter_live)return NVN_MENU_UNSELECTED;
    memset(omit,0,count);initial.reads_valid=0;
    NvnClassicState s=initial;NvnClassicTargets roots={0};
    size_t last=SIZE_MAX;unsigned ui=0,finals=0,scene=0;
    for(size_t i=0;i<count;i++){
        if(r.position)*r.position=i;
        op_hdr h;memcpy(&h,ops[i],8);
        if(!nvn_classic_state(&s,h,ops[i]+8))return NVN_MENU_INVALID;
        if(!nvn_classic_draw(h.op))continue;
        last=i;
        if(!nvn_classic_shader(&s,0,r)||!nvn_classic_shader(&s,1,r)||
            !s.shader[0].present)return NVN_MENU_INVALID;
        if(nvn_menu_ui(&s)||nvn_menu_final(&s)){
            if(s.rt.n!=1||!s.rt.color[0]||s.rt.depth||s.shader[0].effects||s.shader[1].effects)
                {cache->reason=1;cache->detail=((uint64_t)(unsigned)s.rt.n<<32)|((uint64_t)!!s.rt.depth<<16)|(s.shader[0].effects<<8)|s.shader[1].effects;return NVN_MENU_UNSELECTED;}
            nvn_classic_add(&roots,s.rt.color[0]);
            ui+=nvn_menu_ui(&s);finals+=nvn_menu_final(&s);
        }
    }
    if(r.position)*r.position=SIZE_MAX;
    if(last==SIZE_MAX||!ui||finals!=1||roots.overflow){cache->reason=2;cache->detail=((uint64_t)ui<<32)|finals;return NVN_MENU_UNSELECTED;}
    /* The last draw must be the exact original HD UI+scene compositor. */
    s=initial;uint64_t signature=1469598103934665603ull;NvnClassicTargets destinations={0};
    for(size_t i=0;i<count;i++){
        if(r.position)*r.position=i;
        op_hdr h;memcpy(&h,ops[i],8);const uint8_t *a=ops[i]+8;
        if(!nvn_classic_state(&s,h,a))return NVN_MENU_INVALID;
        if(nvn_classic_draw(h.op)){
            if(!nvn_classic_shader(&s,0,r)||!nvn_classic_shader(&s,1,r))return NVN_MENU_INVALID;
            int keep=nvn_menu_ui(&s)||nvn_menu_final(&s);
            if(i==last&&!nvn_menu_final(&s)){cache->reason=3;return NVN_MENU_UNSELECTED;}
            if(!keep){
                if(counter_live[i]||s.shader[0].effects||s.shader[1].effects){cache->reason=4;cache->detail=(counter_live[i]<<16)|(s.shader[0].effects<<8)|s.shader[1].effects;return NVN_MENU_UNSELECTED;}
                if(s.rt.n<1&&!s.rt.depth){cache->reason=6;return NVN_MENU_UNSELECTED;}
                for(int j=0;j<s.rt.n+(s.rt.depth?1:0);j++){
                    const tex_t *t=j==s.rt.n?s.rt.depth:s.rt.color[j];
                    /* Unbound MRT slots map to VK_ATTACHMENT_UNUSED. */
                    if(!t)continue;
                    if(nvn_classic_has(&roots,t->serial)){cache->reason=5;cache->detail=t->serial;return NVN_MENU_UNSELECTED;}
                    if(cache->dynamic_geometry){
                        if(!nvn_classic_has(&destinations,t->serial)){
                            nvn_classic_add(&destinations,t);if(destinations.overflow)return NVN_MENU_UNSELECTED;
                            signature^=nvn_menu_mix(nvn_menu_mix(1469598103934665603ull,t->serial),
                                ((uint64_t)t->d.width<<32)|t->d.height);
                        }
                    }else {
                        signature=nvn_menu_mix(signature,t->serial);
                        signature=nvn_menu_mix(signature,((uint64_t)t->d.width<<32)|t->d.height);
                    }
                }
                /* Role/class previews contain variable particle draw counts.
                 * Their explicit input/context epoch owns scene identity;
                 * stable destination images own the cached 3D result. New
                 * geometry is also sampled by the post-entrance refresh. */
                if(!cache->dynamic_geometry){
                signature=nvn_menu_key(nvn_menu_key(signature,s.shader[0].key),s.shader[1].key);
                /* A streamed/reselected character can bind new materials with
                 * unchanged geometry. Include the used descriptor identities. */
                for(unsigned stage=0;stage<2;stage++)for(unsigned j=0;j<s.shader[stage].nread;j++){
                    unsigned bank=s.shader[stage].read[j].bank,slot=s.shader[stage].read[j].slot;
                    if(bank>1||slot>=(bank?32u:128u))return NVN_MENU_INVALID;
                    signature=nvn_menu_mix(signature,bank?s.image[stage][slot]:s.texture[stage][slot]);
                }
                signature=nvn_menu_mix(signature,h.op);
                /* Draw counts change as streamed characters/scenery arrive;
                 * dynamic ring-buffer addresses and UI animation are excluded. */
                if(h.op==OP_DRAW)signature=nvn_menu_mix(signature,(uint32_t)((const p_draw *)a)->count);
                else if(h.op==OP_DRAW_IDX)signature=nvn_menu_mix(signature,(uint32_t)((const p_draw_idx *)a)->count);
                else signature=nvn_menu_mix(signature,(uint32_t)((const p_draw_instanced *)a)->draw.count);
                }
                omit[i]=1;scene++;
            }
        }else if(h.op==OP_CLEAR_COLOR||h.op==OP_CLEAR_DS){
            if(h.op==OP_CLEAR_COLOR){
                if(h.size<8+sizeof(p_clear_color))return NVN_MENU_INVALID;
                int j=((const p_clear_color *)a)->index;
                if(j<0||j>=s.rt.n||!s.rt.color[j])return NVN_MENU_INVALID;
                if(!nvn_classic_has(&roots,s.rt.color[j]->serial))omit[i]=2;
            }else {
                if(h.size<8+sizeof(p_clear_ds))return NVN_MENU_INVALID;
                if(s.rt.depth&&!nvn_classic_has(&roots,s.rt.depth->serial))omit[i]=2;
            }
        }else if(h.op==OP_DISPATCH){
            if(h.size<8+sizeof(p_dispatch))return NVN_MENU_INVALID;
            if(!nvn_classic_shader(&s,5,r)||!s.shader[5].present)return NVN_MENU_INVALID;
            if(!cache->dynamic_geometry)signature=nvn_menu_key(signature,s.shader[5].key);omit[i]=4;
        }else if(h.op==OP_COPY_T2T){
            if(h.size<8+sizeof(p_copy_t2t))return NVN_MENU_INVALID;
            const p_copy_t2t *c=(const void *)a;
            if(!c->src||!c->dst)return NVN_MENU_INVALID;
            if(!nvn_classic_has(&roots,c->dst->serial))omit[i]=3;
        }else if(h.op==OP_CLEAR_TEX){
            if(h.size<8+sizeof(p_clear_tex))return NVN_MENU_INVALID;
            const tex_t *t=((const p_clear_tex *)a)->tex;
            if(!t)return NVN_MENU_INVALID;
            if(!nvn_classic_has(&roots,t->serial))omit[i]=2;
        }
    }
    if(r.position)*r.position=SIZE_MAX;
    if(!scene)return NVN_MENU_UNSELECTED;
    /* New targets initialize immediately. Input on an already settled preview
     * instead coalesces into one delayed redraw, without another settle refresh. */
    if(cache->signature!=signature){cache->signature=signature;cache->stable=0;cache->refresh=0;cache->frame=frame;return NVN_MENU_WARM;}
    if(cache->frame!=frame){
        cache->frame=frame;if(cache->stable<=NVN_MENU_SETTLE_FRAMES)cache->stable++;
        if(cache->refresh&&!--cache->refresh){cache->stable=NVN_MENU_SETTLE_FRAMES;return NVN_MENU_WARM;}
    }
    if(cache->refresh)return NVN_MENU_REUSE;
    /* UI ticks and authored camera/player movers keep advancing during reuse.
     * Refresh after their entrance transition, so an early empty/mid-move
     * snapshot cannot become the permanent static scene. */
    if(cache->stable==NVN_MENU_SETTLE_FRAMES)return NVN_MENU_WARM;
    unsigned warm=cache->dynamic_geometry?NVN_MENU_DYNAMIC_WARM_FRAMES:NVN_MENU_WARM_FRAMES;
    return cache->stable>=warm?NVN_MENU_REUSE:NVN_MENU_WARM;
}
