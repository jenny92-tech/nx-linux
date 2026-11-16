// INPUT: Requested format-6 cuts, classic admission and the last visible world frame.
// OUTPUT: Rendering cuts during loading; world/pose updates only freeze during play.
// POS: Prevents a classic loading compositor from freezing respawn/level initialization.
#pragma once
static inline unsigned dm_native_hd_gated_mask(unsigned requested,int admitted,int world_ready){
    if(!admitted)return 0;
    /* Only the three reviewed geometry submissions may remain cut while a
     * world is absent. The upper five calls can have initialization effects. */
    return requested&(world_ready?255u:7u);
}
