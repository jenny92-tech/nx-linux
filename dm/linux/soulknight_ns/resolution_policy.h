/* Engine-owned resize, with equal X/Y scale and a bounded known mode family.
 * Display aspect selection remains independent of quality/physical panel size. */
#pragma once
#include <string.h>
static inline int soul_resolution_policy(const char *aspect,const char *scale,int *width,int *height) {
    int base=360;
    if(aspect&&strcmp(aspect,"16:9")){if(strcmp(aspect,"4:3"))return 0;base=480;}
    int numerator=4;
    if(scale&&strcmp(scale,"1")&&strcmp(scale,"1.0")){
        if(!strcmp(scale,".75")||!strcmp(scale,"0.75"))numerator=3;
        else if(!strcmp(scale,".5")||!strcmp(scale,"0.5"))numerator=2;
        else return 0;
    }
    *width=640*numerator/4;*height=base*numerator/4;return 1;
}
