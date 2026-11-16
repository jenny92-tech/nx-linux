// INPUT: Shadow arrays, screen/color targets and independent per-epoch clear layers.
// OUTPUT: Wrong targets, stale clears and uncleared array layers never qualify.
// POS: Host-only guards for the optional visual cut; device A/B remains required.
#include "nvn_vk_shadow_quality.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    tex_t texture={0};texture.d.target=NVN_TEXTURE_TARGET_2D_ARRAY;
    texture.d.width=texture.d.height=512;texture.d.levels=1;
    texture.d.format=NVN_FORMAT_DEPTH16;texture.L.layers=2;
    p_set_rt rt={.depth=&texture};unsigned layer=99;
    assert(nvn_vk_shadow_target(&rt,960,720,&layer)&&layer==0);
    rt.has_dview=1;rt.dview.set_layers=1;rt.dview.min_layer=1;rt.dview.nlayers=1;
    assert(nvn_vk_shadow_target(&rt,960,720,&layer)&&layer==1);
    NvnVkShadowClear clear={0};assert(!nvn_vk_shadow_cleared(&clear,1,0));
    nvn_vk_shadow_note_clear(&clear,1,0);
    assert(nvn_vk_shadow_cleared(&clear,1,0));assert(!nvn_vk_shadow_cleared(&clear,1,1));
    nvn_vk_shadow_note_clear(&clear,1,1);assert(nvn_vk_shadow_cleared(&clear,1,1));
    nvn_vk_shadow_note_clear(&clear,2,1);
    assert(!nvn_vk_shadow_cleared(&clear,1,1));assert(!nvn_vk_shadow_cleared(&clear,2,0));
    assert(nvn_vk_shadow_cleared(&clear,2,1));
    nvn_vk_shadow_note_clear(&clear,0,0);nvn_vk_shadow_note_clear(&clear,2,2);
    assert(!nvn_vk_shadow_cleared(&clear,2,0));assert(!nvn_vk_shadow_cleared(&clear,2,2));
    rt.n=1;assert(!nvn_vk_shadow_target(&rt,960,720,&layer));rt.n=0;
    texture.d.width=960;texture.d.height=720;
    assert(!nvn_vk_shadow_target(&rt,960,720,&layer));
    texture.d.width=texture.d.height=512;
    texture.d.format=NVN_FORMAT_DEPTH24_STENCIL8;
    assert(!nvn_vk_shadow_target(&rt,960,720,&layer));texture.d.format=NVN_FORMAT_DEPTH16;
    rt.dview.nlayers=2;assert(!nvn_vk_shadow_target(&rt,960,720,&layer));rt.dview.nlayers=1;
    rt.dview.min_layer=2;assert(!nvn_vk_shadow_target(&rt,960,720,&layer));rt.dview.min_layer=1;
    rt.dview.set_levels=1;rt.dview.base_level=1;rt.dview.nlevels=1;
    assert(!nvn_vk_shadow_target(&rt,960,720,&layer));rt.dview.base_level=0;
    assert(nvn_vk_shadow_target(&rt,960,720,&layer));
    assert(!nvn_vk_shadow_target(&rt,1280,720,&layer));
    assert(nvn_vk_shadow_vertex("90f80bfd53691ff1"));
    assert(nvn_vk_shadow_vertex("e10d93e9f8e316d5"));
    assert(!nvn_vk_shadow_vertex("8b9bcccd2c97aae4"));
    assert(!nvn_vk_shadow_vertex(NULL));
    puts("shadow quality target/clear guards: passed");return 0;
}
