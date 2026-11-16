#include "../resolution_policy.h"
#include "../aspect_policy.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    const char *scales[]={"1",".75",".5"};int widths[]={640,480,320};
    for(unsigned i=0;i<3;i++)for(unsigned aspect=0;aspect<2;aspect++){
        int w=0,h=0;assert(soul_resolution_policy(aspect?"4:3":"16:9",scales[i],&w,&h));
        assert(w==widths[i]&&w*(aspect?3:9)==h*(aspect?4:16));
        uint32_t out[10];if(aspect)assert(soul_aspect_stub_size(out,0x100000,0x100100,w,h));
    }
    int w,h;assert(soul_resolution_policy(NULL,NULL,&w,&h)&&w==640&&h==360);
    assert(!soul_resolution_policy("5:4","1",&w,&h));assert(!soul_resolution_policy("4:3",".74",&w,&h));
    uint32_t out[10];assert(!soul_aspect_stub_size(out,0x100000,0x100100,640,481));
    assert(soul_aspect_stub(out,0x100000,0x100100));puts("six aspect/quality modes and original predicate family passed");return 0;
}
