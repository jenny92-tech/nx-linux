/* INPUT: Validated SPIR-V and the renderer-owned driver bank descriptors.
 * OUTPUT: Exact static word loads through bounded push constants, or fallback.
 * POS: No guest/material constants are baked; live NVN state supplies each word. */
#ifndef NVN_VK_DRIVER_PUSH_H
#define NVN_VK_DRIVER_PUSH_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef struct {uint32_t set,binding,bank;int enabled;} NvnVkDriverResource;
typedef struct {uint32_t bank,offset;} NvnVkDriverWord;
typedef struct {
    uint32_t count,offset;
    NvnVkDriverWord words[32];
    uint64_t bindings;
} NvnVkDriverPush;

static inline int nvn_vk_driver_push(const uint32_t *w,size_t count,
    const NvnVkDriverResource *resources,size_t nresources,uint32_t limit,uint32_t offset,
    uint32_t **output,size_t *output_count,NvnVkDriverPush *plan) {
    *output=NULL;*output_count=0;memset(plan,0,sizeof *plan);
    if(!w||count<5||w[0]!=0x07230203||w[1]>0x10300||!w[3]||w[3]>1000000||
       nresources>64||(nresources&&!resources)||!limit||limit>32||offset%4)return 0;
    uint32_t bound=w[3],uint_type=0,loads=0;size_t declarations=0,annotations=0;int function_open=0;
    uint32_t *sets=malloc((size_t)bound*4),*bindings=malloc((size_t)bound*4);
    uint32_t *literal=calloc(bound,4),*root=calloc(bound,4),*ptr=calloc(bound,4);
    uint32_t *stride=calloc(bound,4);const uint32_t **types=calloc(bound,sizeof *types);
    uint8_t *known=calloc(bound,1),*scalar=calloc(bound,1),*layout=calloc(bound,1);
    if(!sets||!bindings||!literal||!root||!ptr||!known||!scalar||!stride||!types||!layout)goto reject;
    for(uint32_t i=0;i<bound;i++)sets[i]=bindings[i]=UINT32_MAX;
    for(size_t at=5;at<count;){
        const uint32_t *a=w+at;uint32_t n=a[0]>>16,op=a[0]&65535;
        if(!n||n>count-at)goto reject;
        if(!annotations&&op>=19&&op<=39)annotations=at;
        if(!declarations&&op==54)declarations=at;
        if(op==54){if(function_open)goto reject;function_open=1;}
        if(op==56){if(!function_open)goto reject;function_open=0;}
        if(op==59&&n>=4&&a[3]==9)goto reject; /* Existing push block. */
        if(op==71&&n==4&&a[1]<bound){
            if(a[2]==33)bindings[a[1]]=a[3];
            if(a[2]==34)sets[a[1]]=a[3];
            if(a[2]==6)stride[a[1]]=a[3];
        }
        if(op==71&&n==3&&a[1]<bound&&a[2]==2)layout[a[1]]|=1;
        if(op==72&&n==5&&a[1]<bound&&!a[2]&&a[3]==35&&!a[4])layout[a[1]]|=2;
        if(op>=19&&op<=33&&n>=2&&a[1]<bound)types[a[1]]=a;
        if(((op==21&&n==4)||(op==22&&n==3))&&a[1]<bound&&a[2]==32)scalar[a[1]]=1;
        if(op==21&&n==4&&a[2]==32&&!a[3])uint_type=a[1];
        if(op==43&&n==4&&a[2]<bound){known[a[2]]=1;literal[a[2]]=a[3];}
        at+=n;
    }
    if(!annotations||!declarations||function_open)goto reject;
    for(size_t at=5;at<declarations;){
        const uint32_t *a=w+at;uint32_t n=a[0]>>16,op=a[0]&65535;
        if(op==59&&n>=4&&a[2]<bound&&a[3]==2)
            for(size_t r=0;r<nresources;r++)if(resources[r].enabled&&
                resources[r].set==sets[a[2]]&&resources[r].binding==bindings[a[2]]){
                if(resources[r].bank!=0&&resources[r].bank!=2)goto reject;
                /* The driver ABI is Block { vec4[4096] } at offset zero.
                 * Never derive byte offsets from an unreviewed SPIR-V layout. */
                if(a[1]>=bound||!types[a[1]])goto reject;
                const uint32_t *p=types[a[1]];
                if(p[0]!=(4u<<16|32)||p[2]!=2||p[3]>=bound||!types[p[3]])goto reject;
                const uint32_t *s=types[p[3]];
                if(s[0]!=(3u<<16|30)||layout[p[3]]!=3||s[2]>=bound||!types[s[2]]||stride[s[2]]!=16)goto reject;
                const uint32_t *ar=types[s[2]];
                if(ar[0]!=(4u<<16|28)||ar[3]>=bound||!known[ar[3]]||literal[ar[3]]!=4096||ar[2]>=bound||!types[ar[2]])goto reject;
                const uint32_t *v=types[ar[2]];
                if(v[0]!=(4u<<16|23)||v[3]!=4||v[2]>=bound||!scalar[v[2]])goto reject;
                root[a[2]]=resources[r].bank+1;plan->bindings|=1ull<<r;
            }
        at+=n;
    }
    for(size_t at=declarations;at<count;){
        const uint32_t *a=w+at;uint32_t n=a[0]>>16,op=a[0]&65535;
        if((op==65||op==66)&&n>=4&&a[3]<bound&&root[a[3]]){
            if(n!=7||a[2]>=bound||a[4]>=bound||a[5]>=bound||a[6]>=bound||
               !known[a[4]]||literal[a[4]]||!known[a[5]]||!known[a[6]]||literal[a[6]]>3)goto reject;
            uint64_t byte=(uint64_t)literal[a[5]]*16+literal[a[6]]*4;
            if(byte>0x964)goto reject;
            NvnVkDriverWord field={root[a[3]]-1,(uint32_t)byte};uint32_t i=0;
            for(;i<plan->count;i++)if(plan->words[i].bank==field.bank&&plan->words[i].offset==field.offset)break;
            if(i==plan->count){if(i==limit)goto reject;plan->words[plan->count++]=field;}
            ptr[a[2]]=i+1;
        }
        at+=n;
    }
    /* Prove all function-level pointer uses. Unknown pointer escapes, aliases,
     * volatile loads, whole-block/vector loads and dynamic indices fall back.
     * Literal/ID coincidences can conservatively reject an eligible shader. */
    for(size_t at=declarations;at<count;){
        const uint32_t *a=w+at;uint32_t n=a[0]>>16,op=a[0]&65535;
        if(op==61&&n>=4&&a[3]<bound&&ptr[a[3]]){
            if(n!=4||a[1]>=bound||!scalar[a[1]])goto reject;
            loads++;
        }
        for(uint32_t i=1;i<n;i++)if(a[i]<bound&&(root[a[i]]||ptr[a[i]])){
            if(op==12&&i==4)continue; /* OpExtInst instruction number is a literal. */
            if((op==65||op==66)&&((i==2&&ptr[a[i]])||(i==3&&root[a[i]])))continue;
            if(op==61&&i==3&&ptr[a[i]])continue;
            goto reject;
        }
        at+=n;
    }
    if(!plan->count||!loads||bound>UINT32_MAX-64-loads*2)goto reject;
    if(count>SIZE_MAX/4-256-(size_t)loads*10)goto reject;
    uint32_t *out=malloc((count+256+(size_t)loads*10)*4);if(!out)goto reject;
    uint32_t next=bound,zero=next++,length=next++,array=next++,block=next++;
    uint32_t pblock=next++,pword=next++,variable=next++;int new_uint=!uint_type;
    if(new_uint)uint_type=next++;
    uint32_t index_base=next;
    size_t used=5;memcpy(out,w,20);
#define DP_EMIT(...) do {const uint32_t x[]={__VA_ARGS__};memcpy(out+used,x,sizeof x);used+=sizeof x/4;}while(0)
    for(size_t at=5;at<count;){
        const uint32_t *a=w+at;uint32_t n=a[0]>>16,op=a[0]&65535;
        if(at==annotations){
            DP_EMIT(4u<<16|71,array,6,4); /* ArrayStride */
            DP_EMIT(3u<<16|71,block,2); /* Block */
            DP_EMIT(5u<<16|72,block,0,35,offset); /* Offset */
        }
        if(at==declarations){
            if(new_uint)DP_EMIT(4u<<16|21,uint_type,32,0);
            DP_EMIT(4u<<16|43,uint_type,zero,0);
            DP_EMIT(4u<<16|43,uint_type,length,plan->count);
            DP_EMIT(4u<<16|28,array,uint_type,length);
            DP_EMIT(3u<<16|30,block,array);
            DP_EMIT(4u<<16|32,pblock,9,block);
            DP_EMIT(4u<<16|32,pword,9,uint_type);
            DP_EMIT(4u<<16|59,pblock,variable,9);
            /* Static element indices are declarations, not function literals. */
            for(uint32_t i=0;i<plan->count;i++)DP_EMIT(4u<<16|43,uint_type,next+i,i);
            next+=plan->count;
        }
        if(op==61&&n==4&&a[3]<bound&&ptr[a[3]]){
            uint32_t pointer=next++,value=next++,index=index_base+ptr[a[3]]-1;
            DP_EMIT(6u<<16|65,pword,pointer,variable,zero,index);
            DP_EMIT(4u<<16|61,uint_type,value,pointer);
            DP_EMIT(4u<<16|(a[1]==uint_type?83:124),a[1],a[2],value);
        }else{memcpy(out+used,a,n*4);used+=n;}
        at+=n;
    }
#undef DP_EMIT
    out[3]=next;plan->offset=offset;*output=out;*output_count=used;
    free(sets);free(bindings);free(literal);free(root);free(ptr);free(known);free(scalar);free(stride);free(types);free(layout);return 1;
reject:
    free(sets);free(bindings);free(literal);free(root);free(ptr);free(known);free(scalar);free(stride);free(types);free(layout);
    memset(plan,0,sizeof *plan);return 0;
}
#endif
