// INPUT: Synthetic odd-sized, compressed, array and 3D NVN layouts.
// OUTPUT: Optimized transfers equal independent per-byte reference addresses.
// POS: Host sanitizer regression; shared swizzle/deswizzle bugs cannot cancel out.
#include "../../gfx/tex/nvn_deswizzle.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void image(nvn_texdesc d) {
    nvn_texlayout l;nvn_tex_layout(&d,&l);assert(l.size&&l.size<64u*1024u*1024u);
    uint8_t *reference=malloc(l.size+64),*actual=malloc(l.size+64);
    assert(reference&&actual);memset(reference,0xcd,l.size+64);memset(actual,0xcd,l.size+64);
    for(unsigned layer=0;layer<l.layers;layer++)for(unsigned level=0;level<d.levels;level++) {
        uint32_t rb,rows,slices,bpb;uint64_t base;unsigned th,td;
        assert(!nvn_deswizzle_geom(&d,level,layer,&rb,&rows,&slices,&base,&th,&td,&bpb));
        size_t pitch=rb+13,bytes=pitch*rows*slices;
        uint8_t *linear=malloc(bytes+64),*decoded=malloc(bytes+64);assert(linear&&decoded);
        memset(linear,0xa7,bytes+64);memset(decoded,0xa7,bytes+64);
        for(uint32_t z=0;z<slices;z++)for(uint32_t y=0;y<rows;y++)for(uint32_t x=0;x<rb;x++) {
            uint8_t value=(uint8_t)((x*71u+y*13u+z*29u+level*41u+layer*53u)^(x>>4));
            size_t index=((size_t)z*rows+y)*pitch+x;
            uint64_t address=base+nvn_bl_offset(x,y,z,rb,rows,th,td);assert(address<l.size);
            linear[index]=value;reference[address]=value;
        }
        assert(!nvn_swizzle(&d,level,layer,linear,pitch,actual));
        assert(!nvn_deswizzle(&d,level,layer,reference,decoded,pitch));
        assert(!memcmp(linear,decoded,bytes+64));
        free(decoded);free(linear);
    }
    assert(!memcmp(reference,actual,l.size+64));free(actual);free(reference);
}
int main(void) {
    const int formats[]={0x1,0xd,0x25,0x29,0x2e,0x42,0x44,0x79};
    const unsigned widths[]={1,3,15,16,17,31,63,64,65,127,129,300};
    unsigned checks=0;
    for(unsigned f=0;f<sizeof formats/sizeof *formats;f++)
    for(unsigned w=0;w<sizeof widths/sizeof *widths;w++)
    for(unsigned target=1;target<=4;target++) {
        if(target==3)continue;
        nvn_texdesc d={.target=(int)target,.format=formats[f],.width=widths[w],.height=37,
            .depth=target==1?1:target==2?5:3,.levels=4,.pkg_block_h=(int)(w%6)};
        image(d);checks++;
    }
    image((nvn_texdesc){.target=2,.format=1,.width=64,.height=66,.depth=20,.levels=4});checks++;
    printf("texture layout: %u independent full/partial GOB, mip, layer, depth cases passed\n",checks);return 0;
}
