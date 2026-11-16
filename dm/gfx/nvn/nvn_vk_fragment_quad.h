/* The proved 94012 material variants exchange FP32 bits with horizontal or
 * vertical quad neighbours. Source lane is lane^1/2 with clamp=3, mask=28,
 * so the guest 32-lane validity test is always true. Fragment subgroup shuffle
 * is optional on Vulkan; use fine derivatives of two exact 16-bit integers.
 * Differencing/reconstruction is exact in FP32 even for NaN/Inf payloads in
 * the original value. Never differentiate the original floating-point value.
 * Exact module fingerprints bound the offline dataflow/control-flow proof in
 * android_fragment_quad_profiles.py; unknown modules keep failing closed. */
#ifndef NVN_VK_FRAGMENT_QUAD_H
#define NVN_VK_FRAGMENT_QUAD_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {uint32_t *w,next;size_t used,capacity;int failed;} NvnQuadWriter;
static void nvn_quad_words(NvnQuadWriter *out,const uint32_t *words,size_t n) {
    if(out->failed||n>out->capacity-out->used){out->failed=1;return;}
    memcpy(out->w+out->used,words,n*4);out->used+=n;
}
static uint32_t nvn_quad_value(NvnQuadWriter *out,unsigned op,uint32_t type,const uint32_t *args,size_t n) {
    uint32_t id=out->next++,header[]={(uint32_t)(n+3)<<16|op,type,id};
    nvn_quad_words(out,header,3);nvn_quad_words(out,args,n);return id;
}
#define NVN_QUAD_VALUE(out,op,type,...) nvn_quad_value(out,op,type,(uint32_t[]){__VA_ARGS__},sizeof((uint32_t[]){__VA_ARGS__})/4)
typedef struct {uint32_t fp,u32,boolean,vec4,coord,zero,one,shift,mask;} NvnQuadTypes;
/* Emits the exact bit exchange shared by the production profile and GPU test. */
static void nvn_quad_exchange(NvnQuadWriter *out,const NvnQuadTypes *t,uint32_t input,uint32_t result,unsigned axis) {
    uint32_t coord=NVN_QUAD_VALUE(out,61,t->vec4,t->coord);
    uint32_t y=NVN_QUAD_VALUE(out,81,t->fp,coord,axis);
    uint32_t iy=NVN_QUAD_VALUE(out,109,t->u32,y);
    uint32_t parity=NVN_QUAD_VALUE(out,199,t->u32,iy,t->one);
    uint32_t odd=NVN_QUAD_VALUE(out,171,t->boolean,parity,t->zero);
    uint32_t bits=NVN_QUAD_VALUE(out,124,t->u32,input);
    uint32_t parts[]={NVN_QUAD_VALUE(out,199,t->u32,bits,t->mask),NVN_QUAD_VALUE(out,194,t->u32,bits,t->shift)};
    for(unsigned i=0;i<2;i++){
        uint32_t value=NVN_QUAD_VALUE(out,112,t->fp,parts[i]);
        uint32_t dy=NVN_QUAD_VALUE(out,axis?211:210,t->fp,value);
        uint32_t negative=NVN_QUAD_VALUE(out,127,t->fp,dy);
        uint32_t delta=NVN_QUAD_VALUE(out,169,t->fp,odd,negative,dy);
        uint32_t neighbour=NVN_QUAD_VALUE(out,129,t->fp,value,delta);
        parts[i]=NVN_QUAD_VALUE(out,109,t->u32,neighbour);
    }
    uint32_t high=NVN_QUAD_VALUE(out,196,t->u32,parts[1],t->shift);
    uint32_t combined=NVN_QUAD_VALUE(out,197,t->u32,parts[0],high);
    uint32_t copy[]={4u<<16|124,t->fp,result,combined};nvn_quad_words(out,copy,4);
}
#undef NVN_QUAD_VALUE

typedef struct {
    const char *key;uint64_t hash;uint32_t bytes,fp,u32,boolean,vec4,coord,lane;
    uint8_t swaps,hoist,axes[3];
} NvnQuadProfile;
static const NvnQuadProfile nvn_quad_profiles[]={
#include "nvn_vk_fragment_quad_94012.inc"
};
static const NvnQuadProfile *nvn_vk_fragment_quad_profile(const char *key) {
    for(size_t i=0;i<sizeof nvn_quad_profiles/sizeof *nvn_quad_profiles;i++)
        if(!strcmp(key,nvn_quad_profiles[i].key))return nvn_quad_profiles+i;
    return NULL;
}
/* Owns/replaces heap bytes only on success. No byte changes on rejection. */
static uint8_t *nvn_vk_fragment_quad_apply(uint8_t *bytes,size_t *size,const NvnQuadProfile *profile) {
    if(!profile||*size!=profile->bytes)return NULL;
    uint64_t hash=UINT64_C(14695981039346656037);
    for(size_t i=0;i<*size;i++)hash=(hash^bytes[i])*UINT64_C(1099511628211);
    if(hash!=profile->hash)return NULL;
    const uint32_t *w=(const uint32_t *)bytes;size_t words=*size/4;
    NvnQuadWriter out={.next=w[3],.used=5,.capacity=words+profile->swaps*160+64};
    out.w=malloc(out.capacity*4);if(!out.w)return NULL;memcpy(out.w,w,20);
    const uint32_t **defs=calloc(w[3],sizeof *defs);if(!defs){free(out.w);return NULL;}
    const uint32_t *shuffles[3]={0};unsigned count=0;
    for(size_t at=5;at<words;at+=w[at]>>16){unsigned op=w[at]&65535u;
        if(op==61||op==65)defs[w[at+2]]=w+at;
        if(op==345&&count<3)shuffles[count++]=w+at;
    }
    if(count!=profile->swaps){free(defs);free(out.w);return NULL;}
    NvnQuadTypes t={.fp=profile->fp,.u32=profile->u32,.boolean=profile->boolean,.vec4=profile->vec4,.coord=profile->coord};
    t.zero=out.next++;t.one=out.next++;t.shift=out.next++;t.mask=out.next++;
    uint32_t derivative[]={2u<<16|17,51};nvn_quad_words(&out,derivative,2);
    unsigned swaps=0;int first_block=0,hoisted=0;
    for(size_t at=5;at<words;at+=w[at]>>16){
        unsigned n=w[at]>>16,op=w[at]&65535u;
        /* Hoist only immutable interpolated loads that the offline proof
         * traced to an Input variable and constant component indices. Every
         * invocation executes this entry block, including helper lanes. */
        if(first_block&&!hoisted&&op!=59&&op!=8&&op!=317){
            for(unsigned j=0;j<profile->swaps;j++)if(profile->hoist&(1u<<j)){
                const uint32_t *shuffle=shuffles[j],*load=defs[shuffle[4]],*access=defs[load[3]];
                unsigned length=access[0]>>16;uint32_t pointer=out.next++;
                nvn_quad_words(&out,access,2);nvn_quad_words(&out,&pointer,1);nvn_quad_words(&out,access+3,length-3);
                uint32_t value=nvn_quad_value(&out,61,t.fp,&pointer,1);
                nvn_quad_exchange(&out,&t,value,shuffle[2],profile->axes[j]);
            }
            hoisted=1;
        }
        if(op==17&&w[at+1]==65)continue;
        if(op==71&&w[at+1]==profile->lane)continue;
        if(op==59&&w[at+2]==profile->lane)continue;
        if(op==15){uint32_t header=(n-1)<<16|15;nvn_quad_words(&out,&header,1);
            for(unsigned j=1;j<n;j++)if(w[at+j]!=profile->lane)nvn_quad_words(&out,w+at+j,1);
            continue;
        }
        if(op==54){
            const uint32_t ids[]={t.zero,t.one,t.shift,t.mask},values[]={0,1,16,65535};
            for(unsigned j=0;j<4;j++){uint32_t c[]={4u<<16|43,t.u32,ids[j],values[j]};nvn_quad_words(&out,c,4);}
        }
        /* Only the dead index/always-true clamp chain reads this built-in. */
        if(op==61&&w[at+3]==profile->lane){uint32_t c[]={4u<<16|83,t.u32,w[at+2],t.zero};nvn_quad_words(&out,c,4);continue;}
        if(op==345){
            if(!(profile->hoist&(1u<<swaps)))nvn_quad_exchange(&out,&t,w[at+4],w[at+2],profile->axes[swaps]);
            swaps++;continue;
        }
        nvn_quad_words(&out,w+at,n);
        if(op==248&&!first_block)first_block=1;
    }
    free(defs);
    if(out.failed||swaps!=profile->swaps){free(out.w);return NULL;}
    out.w[3]=out.next;free(bytes);*size=out.used*4;return (uint8_t *)out.w;
}
#endif
