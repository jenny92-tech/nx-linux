// INPUT: Verified Linux support UBO ABI and valid SPIR-V; all 73 scales are 1.
// OUTPUT: Static scalar scale loads become exact float-one constants, or fallback.
// POS: Optional alternate shader; original resource ABI and shader remain available.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static inline int nvn_vk_support_scale_backing(const uint8_t *data,size_t bytes) {
    if(!data||bytes!=1392)return 0;
    for(unsigned i=0;i<73;i++){uint32_t bits;memcpy(&bits,data+192+i*16,4);if(bits!=0x3f800000)return 0;}
    return 1;
}
static inline int nvn_vk_support_scale(const uint32_t *w,size_t count,uint32_t **out,size_t *out_count) {
    *out=NULL;*out_count=0;
    if(!w||count<5||w[0]!=0x07230203||w[1]>0x10300||!w[3]||w[3]>1000000)return 0;
    uint32_t bound=w[3],variable=0,scalar=0;size_t first_function=0;unsigned hits=0;int opened=0;
    const uint32_t **type=calloc(bound,sizeof *type);
    uint32_t *set=malloc((size_t)bound*4),*binding=malloc((size_t)bound*4),*literal=calloc(bound,4),*stride=calloc(bound,4);
    uint8_t *known=calloc(bound,1),*layout=calloc(bound,1),*scale_pointer=calloc(bound,1);
    if(!type||!set||!binding||!literal||!stride||!known||!layout||!scale_pointer)goto done;
    for(uint32_t i=0;i<bound;i++)set[i]=binding[i]=UINT32_MAX;
    for(size_t at=5;at<count;){const uint32_t *a=w+at;unsigned n=a[0]>>16,op=a[0]&65535;
        if(!n||n>count-at)goto done;
        if(op==54){if(opened)goto done;opened=1;if(!first_function)first_function=at;}
        if(op==56){if(!opened)goto done;opened=0;}
        if(op>=19&&op<=33&&n>=2&&a[1]<bound)type[a[1]]=a;
        if(op==43&&n==4&&a[2]<bound){literal[a[2]]=a[3];known[a[2]]=1;}
        if(op==71&&n==4&&a[1]<bound){if(a[2]==34)set[a[1]]=a[3];if(a[2]==33)binding[a[1]]=a[3];if(a[2]==6)stride[a[1]]=a[3];}
        if(op==71&&n==3&&a[1]<bound&&a[2]==2)layout[a[1]]|=1;
        if(op==72&&n==5&&a[1]<bound&&a[2]==5&&a[3]==35&&a[4]==192)layout[a[1]]|=2;
        at+=n;
    }
    if(opened||!first_function)goto done;
    for(size_t at=5;at<first_function;){const uint32_t *a=w+at;unsigned n=a[0]>>16,op=a[0]&65535;
        if(op==59&&n==4&&a[2]<bound&&set[a[2]]==1&&binding[a[2]]==0){
            if(variable||a[3]!=2||a[1]>=bound||!type[a[1]])goto done;
            const uint32_t *p=type[a[1]];
            if(p[0]!=(4u<<16|32)||p[2]!=2||p[3]>=bound||!type[p[3]])goto done;
            const uint32_t *s=type[p[3]];
            if((s[0]&65535)!=30||(s[0]>>16)!=10||layout[p[3]]!=3||s[7]>=bound||!type[s[7]])goto done;
            const uint32_t *array=type[s[7]];
            if(array[0]!=(4u<<16|28)||stride[s[7]]!=16||array[3]>=bound||!known[array[3]]||literal[array[3]]!=73||array[2]>=bound||!type[array[2]])goto done;
            const uint32_t *f=type[array[2]];if(f[0]!=(3u<<16|22)||f[2]!=32)goto done;
            variable=a[2];scalar=array[2];
        }at+=n;
    }
    if(!variable)goto done;
    for(size_t at=first_function;at<count;){const uint32_t *a=w+at;unsigned n=a[0]>>16,op=a[0]&65535;
        if((op==65||op==66)&&n==6&&a[3]==variable&&a[2]<bound&&a[4]<bound&&a[5]<bound&&known[a[4]]&&known[a[5]]&&literal[a[4]]==5&&literal[a[5]]<73&&a[1]<bound&&type[a[1]]){
            const uint32_t *p=type[a[1]];if(p[0]==(4u<<16|32)&&p[2]==2&&p[3]==scalar)scale_pointer[a[2]]=1;
        }
        if(op==61&&n==4&&a[1]==scalar&&a[3]<bound&&scale_pointer[a[3]])hits++;
        at+=n;
    }
    if(hits){uint32_t *result=malloc((count+4)*4);if(!result)goto done;size_t dst=5;memcpy(result,w,20);result[3]=bound+1;
        for(size_t at=5;at<count;){const uint32_t *a=w+at;unsigned n=a[0]>>16,op=a[0]&65535;
            if(at==first_function){uint32_t c[]={4u<<16|43,scalar,bound,0x3f800000};memcpy(result+dst,c,16);dst+=4;}
            memcpy(result+dst,a,n*4);
            if(op==61&&n==4&&a[1]==scalar&&a[3]<bound&&scale_pointer[a[3]]){result[dst]=4u<<16|83;result[dst+3]=bound;}
            dst+=n;at+=n;
        }*out=result;*out_count=dst;
    }
done:
    free(type);free(set);free(binding);free(literal);free(stride);free(known);free(layout);free(scale_pointer);return *out!=NULL;
}
