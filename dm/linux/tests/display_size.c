// INPUT: Common handheld drawable sizes and larger external outputs.
// OUTPUT: Native small modes and bounded, aspect-preserving larger modes.
// POS: Display policy regression; no SDL, device or game data required.
#include "../display_size.h"
#include <assert.h>

int main(void) {
    const int native[][2]={{640,480},{960,720},{1280,720},{1280,800},{1920,1080},{480,480}};
    for(unsigned i=0;i<sizeof native/sizeof *native;i++)
        assert(dm_linux_display_extent(native[i][0],native[i][1])==
            ((uint64_t)native[i][1]<<32|(unsigned)native[i][0]));
    assert(dm_linux_display_extent(1920,1200)==((uint64_t)1080<<32|1728));
    assert(dm_linux_display_extent(2560,1440)==((uint64_t)1080<<32|1920));
    assert(dm_linux_display_extent(720,1280)==((uint64_t)1080<<32|607));
    assert(!dm_linux_display_extent(0,720));
    assert(!dm_linux_display_extent(960,-1));
    return 0;
}
