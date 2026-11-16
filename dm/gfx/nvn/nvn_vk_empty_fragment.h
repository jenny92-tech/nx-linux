/* INPUT: Validated SPIR-V for a single fragment entry point.
 * OUTPUT: Strict proof that its sole function only returns, with no outputs.
 * POS: Optional depth-only fast path; unknown modes/instructions fail closed. */
#ifndef NVN_VK_EMPTY_FRAGMENT_H
#define NVN_VK_EMPTY_FRAGMENT_H
#include <stddef.h>
#include <stdint.h>
static inline int nvn_vk_empty_fragment(const uint32_t *w,size_t count) {
    if(!w||count<5||w[0]!=0x07230203||!w[3])return 0;
    uint32_t entry=0,void_type=0;unsigned entries=0,modes=0,phase=0;
    for(size_t at=5;at<count;) {
        uint32_t n=w[at]>>16,op=w[at]&65535;
        if(!n||n>count-at)return 0;
        const uint32_t *a=w+at;
        if(op==15) {
            /* Fragment, one entry named main, no interface variables. */
            if(phase||n!=5||a[1]!=4||a[3]!=0x6e69616d||a[4]||entries++)return 0;
            entry=a[2];
        } else if(op==16) {
            if(phase||n!=3||a[1]!=entry||a[2]!=7||modes++)return 0;
        } else if(op==331)return 0; /* OpExecutionModeId: unreviewed semantics. */
        else if(op==19) {
            if(phase||n!=2||void_type)return 0;
            void_type=a[1];
        } else if(op==54) {
            if(phase||n!=5||!entry||a[1]!=void_type||a[2]!=entry||a[3])return 0;
            phase=1;
        } else if(phase==1) {
            if(op!=248||n!=2)return 0;phase=2;
        } else if(phase==2) {
            if(op!=253||n!=1)return 0;phase=3;
        } else if(phase==3) {
            if(op!=56||n!=1)return 0;phase=4;
        } else if(phase==4)return 0;
        at+=n;
    }
    return entries==1&&modes==1&&phase==4;
}
#endif
