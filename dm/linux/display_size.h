// INPUT: SDL drawable pixels after the compositor applies panel rotation/scaling.
// OUTPUT: One aspect-preserving guest extent within the original 1920x1080 limit.
// POS: Linux display selection; independent of model names and physical panel order.
#pragma once
#include <stdint.h>

static inline uint64_t dm_linux_display_extent(int width,int height) {
    if(width<=0||height<=0)return 0;
    uint64_t w=(unsigned)width,h=(unsigned)height;
    if(w>1920||h>1080) {
        if(w*1080>h*1920){h=h*1920/w;w=1920;}
        else {w=w*1080/h;h=1080;}
    }
    if(!w||!h)return 0;
    return h<<32|w;
}
