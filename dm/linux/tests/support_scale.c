// INPUT: Synthetic support descriptors/layouts and optional user-owned SPIR-V.
// OUTPUT: Constant-load rewrite and refusal checks without game resources.
// POS: ASan/UBSan host test; optional two file arguments export a validation artifact.
#include "nvn_vk_support_scale.h"
#include <assert.h>
#include <stdio.h>
static uint32_t fixture[]={
    0x07230203,0x10300,0,30,0,
    4u<<16|71,7,34,1, 4u<<16|71,7,33,0,
    4u<<16|71,4,6,16, 3u<<16|71,5,2,
    5u<<16|72,5,5,35,192,
    4u<<16|21,1,32,0, 3u<<16|22,2,32,
    4u<<16|43,1,3,73, 4u<<16|28,4,2,3,
    10u<<16|30,5,1,1,1,1,1,4,1,1,
    4u<<16|32,6,2,5, 4u<<16|32,8,2,2,
    4u<<16|59,6,7,2, 4u<<16|43,1,9,5,
    4u<<16|43,1,10,1, 2u<<16|19,13, 3u<<16|33,14,13,
    5u<<16|54,13,15,0,14, 2u<<16|248,16,
    6u<<16|65,8,11,7,9,10, 4u<<16|61,2,12,11,
    1u<<16|253,1u<<16|56
};
static size_t instruction(uint32_t *w,unsigned op){for(size_t i=5;i<sizeof fixture/4;i+=w[i]>>16)if((w[i]&65535)==op)return i;assert(0);return 0;}
int main(int argc,char **argv){
    if(argc==3){FILE *f=fopen(argv[1],"rb");assert(f&&!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0&&n%4==0&&!fseek(f,0,SEEK_SET));uint32_t *w=malloc((size_t)n),*out=NULL;size_t count=0;assert(w&&fread(w,1,(size_t)n,f)==(size_t)n);fclose(f);assert(nvn_vk_support_scale(w,(size_t)n/4,&out,&count));f=fopen(argv[2],"wb");assert(f&&fwrite(out,4,count,f)==count);fclose(f);free(w);free(out);return 0;}
    assert(argc==1);uint8_t data[1392]={0};for(unsigned i=0;i<73;i++){uint32_t one=0x3f800000;memcpy(data+192+i*16,&one,4);}
    assert(nvn_vk_support_scale_backing(data,sizeof data));assert(!nvn_vk_support_scale_backing(data,sizeof data-1));data[192+72*16]=1;assert(!nvn_vk_support_scale_backing(data,sizeof data));
    size_t n=sizeof fixture/4,count=0;uint32_t *out=NULL,copy[sizeof fixture/4];memcpy(copy,fixture,sizeof fixture);
    assert(nvn_vk_support_scale(fixture,n,&out,&count)&&count==n+4&&out[3]==31);assert(!memcmp(copy,fixture,sizeof fixture));
    size_t load=instruction(fixture,61),ptr=instruction(fixture,65);assert(out[load+4]==(4u<<16|83)&&out[load+7]==30);free(out);
    for(size_t i=0;i<n;i++){assert(!nvn_vk_support_scale(fixture,i,&out,&count));assert(!out&&!count);}
    copy[ptr+5]=29;assert(!nvn_vk_support_scale(copy,n,&out,&count));memcpy(copy,fixture,sizeof copy);
    copy[7]=33;assert(!nvn_vk_support_scale(copy,n,&out,&count));memcpy(copy,fixture,sizeof copy);
    copy[18]=32;assert(!nvn_vk_support_scale(copy,n,&out,&count));memcpy(copy,fixture,sizeof copy);
    copy[16]=32;assert(!nvn_vk_support_scale(copy,n,&out,&count));memcpy(copy,fixture,sizeof copy);
    copy[24]=196;assert(!nvn_vk_support_scale(copy,n,&out,&count));memcpy(copy,fixture,sizeof copy);
    /* A volatile load is preserved, including the explicit memory operand. */
    uint32_t volatile_load[sizeof fixture/4+1];memcpy(volatile_load,fixture,load*4);volatile_load[load]=5u<<16|61;
    memcpy(volatile_load+load+1,fixture+load+1,12);volatile_load[load+4]=1;memcpy(volatile_load+load+5,fixture+load+4,(n-load-4)*4);
    assert(!nvn_vk_support_scale(volatile_load,n+1,&out,&count));
    puts("support scales: immutable backing, layout/bounds, truncation, dynamic/volatile exclusions passed");
}
