// INPUT: Synthetic reviewed UBO layouts, malformed variants and optional local IR.
// OUTPUT: Bit-preserving load substitution, original-byte immutability and guards.
// POS: Host sanitizer regression; optional file transform supports spirv-val QA.
#include "nvn_vk_classic_flags.h"
#include <assert.h>
#include <stdio.h>
#define OP(n,op) ((uint32_t)(n)<<16|(op))
static const uint32_t fixture[]={
    0x07230203,0x10000,0,23,0,
    OP(2,17),1,OP(3,14),0,1,OP(5,15),4,17,0x6e69616d,0,OP(3,16),17,7,
    OP(4,71),7,34,1,OP(4,71),7,33,4,OP(3,71),5,2,OP(5,72),5,0,35,0,OP(4,71),4,6,16,
    OP(4,21),1,32,0,OP(3,22),2,32,OP(4,23),3,2,4,OP(4,43),1,8,4096,
    OP(4,28),4,3,8,OP(3,30),5,4,OP(4,32),6,2,5,OP(4,59),6,7,2,
    OP(4,21),9,32,1,OP(4,43),9,10,0,OP(4,43),9,11,47,OP(4,43),9,12,51,OP(4,43),9,13,3,
    OP(4,32),14,2,2,OP(2,19),15,OP(3,33),16,15,
    OP(5,54),15,17,0,16,OP(2,248),18,
    OP(7,65),14,19,7,10,11,10,OP(4,61),2,20,19,
    OP(7,65),14,21,7,10,12,13,OP(4,61),2,22,21,OP(1,253),OP(1,56)
};
int main(int argc,char **argv){
    NvnVkClassicFlagBinding bindings[]={{1,3,2,1,4},{1,4,2,1,10},{1,5,7,1,1}};
    assert(nvn_vk_classic_flag_slot(bindings,3)==10); /* C13 is renumbered to NVN slot 10. */
    bindings[2]=bindings[1];assert(nvn_vk_classic_flag_slot(bindings,3)==-1);
    bindings[2].binding=5;bindings[1].kind=3;assert(nvn_vk_classic_flag_slot(bindings,3)==-1);
    bindings[1].kind=2;bindings[1].slot=16;assert(nvn_vk_classic_flag_slot(bindings,3)==-1);
    uint32_t original[sizeof fixture/4];memcpy(original,fixture,sizeof fixture);
    uint32_t *out=NULL;size_t n=0;
    assert(nvn_vk_classic_flags(fixture,sizeof fixture/4,&out,&n));assert(n==sizeof fixture/4+16&&out[3]==25);
    unsigned copies=0,specs=0;
    for(size_t at=5;at<n;at+=out[at]>>16){
        if((out[at]&65535)==124){assert(out[at+1]==2);assert(out[at+3]==23+copies);copies++;}
        if((out[at]&65535)==50){assert(out[at+1]==1&&out[at+2]==23+specs&&!out[at+3]);specs++;}
    }
    assert(copies==2&&specs==2&&!memcmp(original,fixture,sizeof fixture));free(out);
    for(size_t i=0;i<sizeof fixture/4;i++)assert(!nvn_vk_classic_flags(fixture,i,&out,&n)&&!out&&!n);
    for(size_t at=5;at<sizeof fixture/4;at+=fixture[at]>>16){
        uint32_t bad[sizeof fixture/4];memcpy(bad,fixture,sizeof bad);
        if(fixture[at]==OP(4,71)&&fixture[at+2]==6){bad[at+3]=32;assert(!nvn_vk_classic_flags(bad,sizeof bad/4,&out,&n));}
        if(fixture[at]==OP(4,71)&&fixture[at+2]==33){bad[at+3]=5;assert(!nvn_vk_classic_flags(bad,sizeof bad/4,&out,&n));}
        if(fixture[at]==OP(4,43)&&fixture[at+2]==12){bad[at+3]=52;assert(!nvn_vk_classic_flags(bad,sizeof bad/4,&out,&n));}
    }
    if(argc==3){
        FILE *f=fopen(argv[1],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long bytes=ftell(f);assert(bytes>0&&bytes%4==0);rewind(f);
        uint32_t *w=malloc((size_t)bytes);assert(w&&fread(w,1,(size_t)bytes,f)==(size_t)bytes);fclose(f);
        assert(nvn_vk_classic_flags(w,(size_t)bytes/4,&out,&n));free(w);
        f=fopen(argv[2],"wb");assert(f&&fwrite(out,4,n,f)==n&&!fclose(f));free(out);
    }
    puts("classic flags: exact substitutions/layout guards/truncation/immutability passed");
}
