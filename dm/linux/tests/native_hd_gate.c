// INPUT: Every format-6 mask and original-world/loading/admission transitions.
// OUTPUT: No world-update freeze survives a frame without the classic world.
// POS: Host regression for the observed respawn loading stall; no game data.
#include "../native_hd_gate.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    for(unsigned mask=0;mask<256;mask++){
        assert(dm_native_hd_gated_mask(mask,0,0)==0);
        assert(dm_native_hd_gated_mask(mask,0,1)==0);
        assert(dm_native_hd_gated_mask(mask,1,0)==(mask&7));
        assert(dm_native_hd_gated_mask(mask,1,1)==mask);
    }
    assert(dm_native_hd_gated_mask(119,1,1)==119);
    assert(dm_native_hd_gated_mask(119,1,0)==7);
    assert(dm_native_hd_gated_mask(119,0,0)==0);
    assert(dm_native_hd_gated_mask(119,1,0)==7);
    assert(dm_native_hd_gated_mask(119,1,1)==119);
    puts("native HD gate: all masks, loading, rejection and world reentry passed");
}
