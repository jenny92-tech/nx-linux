/* INPUT: Synthetic SPIR-V function/mode records, no private shader data.
 * OUTPUT: Empty-proof rejection checks for effects and malformed input.
 * POS: Host regression for the depth-only optional fragment-stage omission. */
#include "nvn_vk_empty_fragment.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    const uint32_t valid[]={0x07230203,0x10300,0,8,0,
        0x5000f,4,4,0x6e69616d,0,0x30010,4,7,
        0x20013,1,0x30021,2,1,0x50036,1,4,0,2,
        0x200f8,5,0x100fd,0x10038};
    size_t n=sizeof valid/sizeof *valid;uint32_t copy[64];
    assert(nvn_vk_empty_fragment(valid,n));
    assert(!nvn_vk_empty_fragment(NULL,n));
    for(size_t i=0;i<n;i++)assert(!nvn_vk_empty_fragment(valid,i));
    for(size_t i=0;i<n;i++) {
        memcpy(copy,valid,sizeof valid);copy[i]^=0x80000000;
        if(i==1||i==2||i==3||i==4||i==16||i==18||i==24)continue;
        /* Unused IDs can change; malformed headers/structure cannot. */
        if(i==14||i==17||i==22||i==25)continue;
        assert(!nvn_vk_empty_fragment(copy,n));
    }
    memcpy(copy,valid,sizeof valid);copy[n-2]=0x100fc; /* OpKill */
    assert(!nvn_vk_empty_fragment(copy,n));
    memcpy(copy,valid,sizeof valid);copy[12]=12; /* DepthReplacing */
    assert(!nvn_vk_empty_fragment(copy,n));
    memcpy(copy,valid,sizeof valid);copy[n]=0x10000; /* extra instruction */
    assert(!nvn_vk_empty_fragment(copy,n+1));
    puts("empty fragment proof: passed");return 0;
}
