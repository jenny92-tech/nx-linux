// INPUT: Synthetic C3 descriptor and optional user's sprite shader.
// OUTPUT: Selector-only rewrite, layout/volatile/bounds refusal and export artifact.
// POS: Host sanitizer test; no running process or game resources are modified.
#include "nvn_vk_sprite_nearest.h"
#include <assert.h>
#include <stdio.h>
#define OP(n,o) ((uint32_t)(n)<<16|(o))
static uint32_t fixture[]={
    0x07230203,0x10300,0,23,0,
    OP(4,71),7,34,1,OP(4,71),7,33,3,OP(3,71),5,2,OP(5,72),5,0,35,0,OP(4,71),4,6,16,
    OP(4,21),1,32,0,OP(3,22),2,32,OP(4,23),3,2,4,OP(4,43),1,8,4096,
    OP(4,28),4,3,8,OP(3,30),5,4,OP(4,32),6,2,5,OP(4,59),6,7,2,
    OP(4,43),1,10,0,OP(4,43),1,11,1,OP(4,43),1,13,3,
    OP(4,32),14,2,2,OP(2,19),15,OP(3,33),16,15,
    OP(5,54),15,17,0,16,OP(2,248),18,
    OP(7,65),14,19,7,10,11,13,OP(4,61),2,20,19,
    OP(1,253),OP(1,56)
};
int main(int argc,char **argv){
    size_t n=sizeof fixture/4,words=0;uint32_t *out=NULL,copy[sizeof fixture/4];memcpy(copy,fixture,sizeof copy);
    assert(nvn_vk_sprite_nearest(fixture,n,&out,&words)&&words==n+4&&out[3]==24);
    unsigned loads=0;for(size_t at=5;at<words;at+=out[at]>>16)if((out[at]&65535)==83){loads++;assert(out[at+1]==2&&out[at+2]==20&&out[at+3]==23);}
    assert(loads==1&&!memcmp(copy,fixture,sizeof copy));free(out);
    for(size_t i=0;i<n;i++)assert(!nvn_vk_sprite_nearest(fixture,i,&out,&words)&&!out&&!words);
    for(size_t at=5;at<n;at+=fixture[at]>>16){
        memcpy(copy,fixture,sizeof copy);
        if(fixture[at]==OP(4,71)&&fixture[at+2]==33){copy[at+3]=4;assert(!nvn_vk_sprite_nearest(copy,n,&out,&words));}
        if(fixture[at]==OP(4,71)&&fixture[at+2]==6){copy[at+3]=32;assert(!nvn_vk_sprite_nearest(copy,n,&out,&words));}
        if(fixture[at]==OP(7,65)){copy[at+5]=22;assert(!nvn_vk_sprite_nearest(copy,n,&out,&words));}
        if(fixture[at]==OP(4,61)){
            uint32_t v[sizeof fixture/4+1];memcpy(v,fixture,at*4);v[at]=OP(5,61);memcpy(v+at+1,fixture+at+1,12);v[at+4]=1;
            memcpy(v+at+5,fixture+at+4,(n-at-4)*4);assert(!nvn_vk_sprite_nearest(v,n+1,&out,&words));
        }
    }
    if(argc==3){FILE *f=fopen(argv[1],"rb");assert(f&&!fseek(f,0,SEEK_END));long bytes=ftell(f);assert(bytes>0&&bytes%4==0&&!fseek(f,0,SEEK_SET));
        uint32_t *w=malloc((size_t)bytes);assert(w&&fread(w,1,(size_t)bytes,f)==(size_t)bytes);fclose(f);
        assert(nvn_vk_sprite_nearest(w,(size_t)bytes/4,&out,&words));free(w);f=fopen(argv[2],"wb");assert(f&&fwrite(out,4,words,f)==words&&!fclose(f));free(out);
    }else assert(argc==1);
    puts("sprite nearest: selector-only rewrite, bounds/layout/volatile refusal passed");
}
