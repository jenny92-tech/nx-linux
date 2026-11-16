// INPUT: Valid SPIR-V with the reviewed C13 Block { vec4[4096] } descriptor ABI.
// OUTPUT: Two flag loads become bit-exact specialization constants, or fallback.
// POS: Classic compositor optimization; draw-time CPU provenance supplies values.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define NVN_CLASSIC_FLAG_SPEC_BASE 42000u
typedef struct {uint32_t set,binding,kind,count;int32_t slot;} NvnVkClassicFlagBinding;
static inline int nvn_vk_classic_flag_slot(const NvnVkClassicFlagBinding *b,size_t n) {
    int slot=-1;
    if(!b||n>64)return -1;
    for(size_t i=0;i<n;i++)if(b[i].set==1&&b[i].binding==4){
        if(slot>=0||b[i].kind!=2||b[i].count!=1||b[i].slot<0||b[i].slot>=16)return -1;
        slot=b[i].slot;
    }
    return slot;
}
static inline int nvn_vk_classic_flags(const uint32_t *w,size_t count,uint32_t **out,size_t *out_count) {
    *out=NULL;*out_count=0;
    if(!w||count<5||w[0]!=0x07230203||w[1]>0x10300||!w[3]||w[3]>1000000)return 0;
    uint32_t bound=w[3],variable=0,float_type=0,uint_type=0,hits[2]={0};size_t annotations=0,declarations=0;int function_open=0;
    const uint32_t **type=calloc(bound,sizeof *type);
    uint32_t *set=malloc((size_t)bound*4),*binding=malloc((size_t)bound*4),*literal=calloc(bound,4);
    uint32_t *stride=calloc(bound,4),*field=calloc(bound,4);
    uint8_t *known=calloc(bound,1),*layout=calloc(bound,1);
    if(!type||!set||!binding||!literal||!stride||!field||!known||!layout)goto reject;
    for(uint32_t i=0;i<bound;i++)set[i]=binding[i]=UINT32_MAX;
    for(size_t at=5;at<count;){
        const uint32_t *a=w+at;unsigned n=a[0]>>16,op=a[0]&65535;
        if(!n||n>count-at)goto reject;
        if(!annotations&&op>=19&&op<=39)annotations=at;
        if(!declarations&&op==54)declarations=at;
        if(op==54){if(function_open)goto reject;function_open=1;}
        if(op==56){if(!function_open)goto reject;function_open=0;}
        if(op>=19&&op<=33&&n>=2&&a[1]<bound)type[a[1]]=a;
        if(op==21&&n==4&&a[2]==32&&!a[3])uint_type=a[1];
        if(op==43&&n==4&&a[2]<bound){literal[a[2]]=a[3];known[a[2]]=1;}
        if(op==71&&n==4&&a[1]<bound){
            if(a[2]==34)set[a[1]]=a[3];
            if(a[2]==33)binding[a[1]]=a[3];
            if(a[2]==6)stride[a[1]]=a[3];
            if(a[2]==1&&(a[3]==NVN_CLASSIC_FLAG_SPEC_BASE||a[3]==NVN_CLASSIC_FLAG_SPEC_BASE+1))goto reject;
        }
        if(op==71&&n==3&&a[1]<bound&&a[2]==2)layout[a[1]]|=1;
        if(op==72&&n==5&&a[1]<bound&&!a[2]&&a[3]==35&&!a[4])layout[a[1]]|=2;
        at+=n;
    }
    if(!annotations||!declarations||function_open||!uint_type)goto reject;
    for(size_t at=5;at<declarations;){
        const uint32_t *a=w+at;unsigned n=a[0]>>16,op=a[0]&65535;
        if(op==59&&n==4&&a[2]<bound&&set[a[2]]==1&&binding[a[2]]==4){
            if(variable||a[3]!=2||a[1]>=bound||!type[a[1]])goto reject;
            const uint32_t *p=type[a[1]];
            if(p[0]!=(4u<<16|32)||p[2]!=2||p[3]>=bound||!type[p[3]])goto reject;
            const uint32_t *s=type[p[3]];
            if(s[0]!=(3u<<16|30)||layout[p[3]]!=3||s[2]>=bound||!type[s[2]]||stride[s[2]]!=16)goto reject;
            const uint32_t *ar=type[s[2]];
            if(ar[0]!=(4u<<16|28)||ar[3]>=bound||!known[ar[3]]||literal[ar[3]]!=4096||ar[2]>=bound||!type[ar[2]])goto reject;
            const uint32_t *v=type[ar[2]];
            if(v[0]!=(4u<<16|23)||v[3]!=4||v[2]>=bound||!type[v[2]])goto reject;
            const uint32_t *f=type[v[2]];
            if(f[0]!=(3u<<16|22)||f[2]!=32)goto reject;
            variable=a[2];float_type=v[2];
        }
        at+=n;
    }
    if(!variable)goto reject;
    for(size_t at=declarations;at<count;){
        const uint32_t *a=w+at;unsigned n=a[0]>>16,op=a[0]&65535;
        if((op==65||op==66)&&n==7&&a[3]==variable&&a[2]<bound&&
           a[4]<bound&&a[5]<bound&&a[6]<bound&&known[a[4]]&&known[a[5]]&&known[a[6]]&&!literal[a[4]]){
            if(literal[a[5]]==47&&!literal[a[6]])field[a[2]]=1;
            if(literal[a[5]]==51&&literal[a[6]]==3)field[a[2]]=2;
        }
        if(op==61&&n>=4&&a[3]<bound&&field[a[3]]){
            if(n!=4||a[1]!=float_type)goto reject; /* Never replace volatile/extra operands. */
            hits[field[a[3]]-1]++;
        }
        at+=n;
    }
    if(!hits[0]||!hits[1])goto reject;
    size_t words=count+16;uint32_t *result=malloc(words*4);if(!result)goto reject;
    size_t dst=0;memcpy(result,w,20);dst=5;result[3]=bound+2;
    for(size_t at=5;at<count;){
        if(at==annotations)for(unsigned i=0;i<2;i++){
            uint32_t a[]={4u<<16|71,bound+i,1,NVN_CLASSIC_FLAG_SPEC_BASE+i};memcpy(result+dst,a,sizeof a);dst+=4;
        }
        if(at==declarations)for(unsigned i=0;i<2;i++){
            /* Integer specialization preserves all raw flag bits, including
             * patterns that would be floating NaNs. No float canonicalization. */
            uint32_t a[]={4u<<16|50,uint_type,bound+i,0};memcpy(result+dst,a,sizeof a);dst+=4;
        }
        const uint32_t *a=w+at;unsigned n=a[0]>>16,op=a[0]&65535;
        memcpy(result+dst,a,n*4);
        if(op==61&&n==4&&a[3]<bound&&field[a[3]]){result[dst]=4u<<16|124;result[dst+3]=bound+field[a[3]]-1;}
        dst+=n;at+=n;
    }
    *out=result;*out_count=dst;
    free(type);free(set);free(binding);free(literal);free(stride);free(field);free(known);free(layout);return 1;
reject:
    free(type);free(set);free(binding);free(literal);free(stride);free(field);free(known);free(layout);return 0;
}
