#include "../../gfx/nvn/nvn_vk_texture_lifetime.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    NvnTextureRetirement r={{7,19,0}};
    assert(!nvn_texture_retirement_ready(&r));
    assert(!nvn_texture_retirement_done(&r,0,8)); /* reused slot != old fence */
    assert(!nvn_texture_retirement_done(&r,2,0));
    assert(!nvn_texture_retirement_done(&r,3,7));
    assert(nvn_texture_retirement_done(&r,0,7));
    assert(!nvn_texture_retirement_ready(&r));
    assert(!nvn_texture_retirement_done(&r,0,7));
    assert(nvn_texture_retirement_done(&r,1,19));
    assert(nvn_texture_retirement_ready(&r));
    NvnTextureAllocationKey a={1,2,3,640,360,1,1,1,1,0,7,0,1},b=a;
    assert(nvn_texture_allocation_equal(&a,&b));
    uint32_t *words=(uint32_t *)&b;
    for(unsigned i=0;i<sizeof b/sizeof *words;i++){
        b=a;words[i]^=1;assert(!nvn_texture_allocation_equal(&a,&b));
    }
    puts("texture identity and retirement epochs passed");return 0;
}
