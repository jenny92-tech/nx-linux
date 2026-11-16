/* Host proof/validator input and fixture generation for the production emitter.
 * Args: shader-root output-directory (all proved variants), or
 * original-material.spv adapted-material.spv fixture.spv lowered-fixture.spv. */
#include "../../gfx/nvn/nvn_vk_fragment_quad.h"
#include <assert.h>
#include <stdio.h>
static uint8_t *read_file(const char *path,size_t *bytes){
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0);rewind(f);
    uint8_t *p=malloc((size_t)n);assert(p&&fread(p,1,(size_t)n,f)==(size_t)n);fclose(f);*bytes=(size_t)n;return p;
}
static void write_file(const char *path,const void *p,size_t n){FILE *f=fopen(path,"wb");assert(f);assert(fwrite(p,1,n,f)==n);assert(!fclose(f));}
static size_t adapt(const char *source,const char *output,const NvnQuadProfile *profile){
    size_t size;uint8_t *bytes=read_file(source,&size);size_t original=size,checks=0;
    assert(size==profile->bytes);
    for(size_t at=0;at<size;at+=251){bytes[at]^=1;assert(!nvn_vk_fragment_quad_apply(bytes,&size,profile));assert(size==original);bytes[at]^=1;checks++;}
    bytes=nvn_vk_fragment_quad_apply(bytes,&size,profile);assert(bytes);write_file(output,bytes,size);free(bytes);return checks;
}
int main(int argc,char **argv){
    assert(!nvn_vk_fragment_quad_profile("unknown-shader"));
    if(argc==3){
        size_t checks=0,swaps=0;
        for(size_t i=0;i<sizeof nvn_quad_profiles/sizeof *nvn_quad_profiles;i++){
            const NvnQuadProfile *p=nvn_quad_profiles+i;char source[2048],output[2048];
            assert(snprintf(source,sizeof source,"%s/%s.spv",argv[1],p->key)<(int)sizeof source);
            assert(snprintf(output,sizeof output,"%s/%s.spv",argv[2],p->key)<(int)sizeof output);
            checks+=adapt(source,output,p);swaps+=p->swaps;
        }
        printf("FRAGMENT_QUAD_ALL modules=%zu swaps=%zu mutations_rejected=%zu\n",sizeof nvn_quad_profiles/sizeof *nvn_quad_profiles,swaps,checks);return 0;
    }
    assert(argc==5);const NvnQuadProfile *profile=nvn_vk_fragment_quad_profile("0de8529e13ee3bcd");assert(profile);
    size_t checks=adapt(argv[1],argv[2],profile),size;
    uint8_t *bytes=read_file(argv[3],&size);assert(size>=20&&size%4==0);const uint32_t *w=(const uint32_t *)bytes;size_t words=size/4;
    NvnQuadWriter out={.next=w[3],.used=5,.capacity=words+768};out.w=malloc(out.capacity*4);assert(out.w);memcpy(out.w,w,20);
    uint32_t *constants=calloc(w[3],sizeof *constants);assert(constants);
    NvnQuadTypes t={0};uint32_t coord=0;unsigned swaps=0;
    for(size_t at=5;at<words;at+=w[at]>>16){unsigned op=w[at]&65535,n=w[at]>>16;assert(n&&n<=words-at);
        if(op==22&&w[at+2]==32)t.fp=w[at+1];
        if(op==21&&w[at+2]==32&&!w[at+3])t.u32=w[at+1];
        if(op==20)t.boolean=w[at+1];
        if(op==23&&w[at+2]==t.fp&&w[at+3]==4)t.vec4=w[at+1];
        if(op==71&&n==4&&w[at+2]==11&&w[at+3]==15)coord=w[at+1];
        if(op==43&&n==4)constants[w[at+2]]=w[at+3];
    }
    assert(t.fp&&t.u32&&t.vec4&&coord);t.coord=coord;int new_bool=!t.boolean;if(new_bool)t.boolean=out.next++;
    t.zero=out.next++;t.one=out.next++;t.shift=out.next++;t.mask=out.next++;
    uint32_t cap[]={2u<<16|17,51};nvn_quad_words(&out,cap,2);
    int declared=0;
    for(size_t at=5;at<words;at+=w[at]>>16){unsigned op=w[at]&65535,n=w[at]>>16;
        if(op==17&&(w[at+1]==61||w[at+1]==65))continue;
        if(op==54&&!declared){
            if(new_bool){uint32_t b[]={2u<<16|20,t.boolean};nvn_quad_words(&out,b,2);}
            const uint32_t ids[]={t.zero,t.one,t.shift,t.mask},values[]={0,1,16,65535};
            for(unsigned j=0;j<4;j++){uint32_t c[]={4u<<16|43,t.u32,ids[j],values[j]};nvn_quad_words(&out,c,4);}
            declared=1;
        }
        if(op==346){assert(n==6&&w[at+1]==t.fp);unsigned mask=constants[w[at+5]];assert(mask==1||mask==2);
            nvn_quad_exchange(&out,&t,w[at+4],w[at+2],mask-1);swaps++;continue;}
        nvn_quad_words(&out,w+at,n);
    }
    assert(!out.failed&&swaps==2);out.w[3]=out.next;write_file(argv[4],out.w,out.used*4);free(out.w);free(constants);free(bytes);
    printf("FRAGMENT_QUAD_HOST mutations_rejected=%zu material_swaps=2 fixture_swaps=2\n",checks);return 0;
}
