/* One selected size from the reviewed 4:3 family, preserving the original predicate
 * and its NZCV for every other request. x9 is dead at this title-bound site. */
#pragma once
#include <stdint.h>
static int soul_aspect_branch(uintptr_t from,uintptr_t to,uint32_t *out) {
    int64_t delta=(int64_t)to-(int64_t)from;
    if((from|to)&3||delta<-(1ll<<27)||delta>(1ll<<27)-4)return 0;
    *out=0x14000000u|((uint32_t)(delta/4)&0x03ffffffu);return 1;
}
static int soul_aspect_stub_size(uint32_t out[10],uintptr_t address,uintptr_t resume,unsigned width,unsigned height) {
    if(!((width==640&&height==480)||(width==480&&height==360)||(width==320&&height==240)))return 0;
    uint32_t code[10]={
        0xd53b4209, /* mrs x9,nzcv */
        0x710a02bf, /* cmp w21,#640 */
        0x540000a1, /* b.ne original */
        0x7107829f, /* cmp w20,#480 */
        0x54000061, /* b.ne original */
        0x7100011f, /* cmp w8,#0: preserve resize-needed decision */
        0,
        0xd51b4209, /* original: msr nzcv,x9 */
        0x7a409904, /* original ccmp w8,#0,#4,ls */
        0
    };
    code[1]=0x710002bfu|(width<<10);code[3]=0x7100029fu|(height<<10);
    if(!soul_aspect_branch(address+6*4,resume,&code[6])||
       !soul_aspect_branch(address+9*4,resume,&code[9]))return 0;
    for(unsigned i=0;i<10;i++)out[i]=code[i];return 1;
}
static int soul_aspect_stub(uint32_t out[10],uintptr_t address,uintptr_t resume) {
    return soul_aspect_stub_size(out,address,resume,640,480);
}
