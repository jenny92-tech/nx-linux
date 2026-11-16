// INPUT: Reviewed 90447 shadow targets/shaders and same-submission clear evidence.
// OUTPUT: Narrow opt-in shadow geometry eligibility; screen depth is excluded.
// POS: Visual quality policy only; live counters and shader writes stay in replay.
#pragma once
#include "nvn_obj.h"

typedef struct {uint64_t epoch;uint32_t layers;} NvnVkShadowClear;
static inline int nvn_vk_shadow_target(const p_set_rt *rt,unsigned output_width,unsigned output_height,unsigned *layer) {
    const tex_t *t=rt->depth;
    if(output_width!=960||output_height!=720||rt->n||!t||
       t->d.width!=512||t->d.height!=512||t->d.format!=NVN_FORMAT_DEPTH16||
       t->d.levels!=1||t->d.samples>1||!t->L.layers||t->L.layers>2||
       (t->d.target!=NVN_TEXTURE_TARGET_2D&&t->d.target!=NVN_TEXTURE_TARGET_2D_ARRAY))return 0;
    const view_t *v=rt->has_dview?&rt->dview:NULL;
    if(v&&((v->set_levels&&(v->base_level||v->nlevels!=1))||
           (v->set_layers&&(v->min_layer<0||v->nlayers!=1))||
           (v->set_format&&v->format!=NVN_FORMAT_DEPTH16)))return 0;
    unsigned at=v&&v->set_layers?(unsigned)v->min_layer:0;
    if(at>=t->L.layers)return 0;
    *layer=at;return 1;
}
static inline int nvn_vk_shadow_vertex(const char *key) {
    return key&&(!strcmp(key,"90f80bfd53691ff1")||!strcmp(key,"e10d93e9f8e316d5"));
}
static inline void nvn_vk_shadow_note_clear(NvnVkShadowClear *clear,uint64_t epoch,unsigned layer) {
    if(!epoch||layer>=2)return;
    if(clear->epoch!=epoch){clear->epoch=epoch;clear->layers=0;}
    clear->layers|=1u<<layer;
}
static inline int nvn_vk_shadow_cleared(const NvnVkShadowClear *clear,uint64_t epoch,unsigned layer) {
    return epoch&&layer<2&&clear->epoch==epoch&&(clear->layers&(1u<<layer));
}
