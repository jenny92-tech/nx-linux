// INPUT: Active renderer identity, game present callbacks and Linux process RSS.
// OUTPUT: Native scanout status: one-second FPS and half-second RAM samples.
// POS: Optional Linux display overlay; never modifies guest images or resources.
#pragma once
#include <stdint.h>

void dm_linux_status_hud_init(const char *api);
int dm_linux_status_hud_extent(unsigned output_width,unsigned output_height,unsigned *width,unsigned *height);
int dm_linux_status_hud_pixels(uint8_t *pixels,unsigned width,unsigned height,int bgra,uint64_t now_ns);
