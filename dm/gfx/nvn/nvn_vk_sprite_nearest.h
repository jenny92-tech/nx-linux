// INPUT: Reviewed classic sprite SPIR-V with C3 Block { vec4[4096] } ABI.
// OUTPUT: Optional nearest-neighbour quality variant; original shader is retained.
// POS: Only the manual palette-filter selector changes, not textures or alpha tests.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static inline int nvn_vk_sprite_nearest(const uint32_t *w,size_t count,uint32_t **out,size_t *out_count) {
    *out=NULL;*out_count=0;
    if(!w||count<5||w[0]!=0x07230203||w[1]>0x10300||!w[3]||w[3]>1000000)return 0;
    uint32_t bound=w[3],variable=0,scalar=0;size_t first=0;unsigned hits=0;int opened=0;
    const uint32_t **type=calloc(bound,sizeof *type);
    uint32_t *set=malloc((size_t)bound*4),*binding=malloc((size_t)bound*4),*literal=calloc(bound,4),*stride=calloc(bound,4);
    uint8_t *known=calloc(bound,1),*layout=calloc(bound,1),*pointer=calloc(bound,1);
    if(!type||!set||!binding||!literal||!stride||!known||!layout||!pointer)goto done;
    for(uint32_t i=0;i<bound;i++)set[i]=binding[i]=UINT32_MAX;
    for(size_t at=5;at<count;){const uint32_t *a=w+at;unsigned n=a[0]>>16,op=a[0]&65535;
        if(!n||n>count-at)goto done;
        if(op==54){if(opened)goto done;opened=1;if(!first)first=at;}
        if(op==56){if(!opened)goto done;opened=0;}
        if(op>=19&&op<=33&&n>=2&&a[1]<bound)type[a[1]]=a;
        if(op==43&&n==4&&a[2]<bound){literal[a[2]]=a[3];known[a[2]]=1;}
        if(op==71&&n==4&&a[1]<bound){if(a[2]==34)set[a[1]]=a[3];if(a[2]==33)binding[a[1]]=a[3];if(a[2]==6)stride[a[1]]=a[3];}
        if(op==71&&n==3&&a[1]<bound&&a[2]==2)layout[a[1]]|=1;
        if(op==72&&n==5&&a[1]<bound&&!a[2]&&a[3]==35&&!a[4])layout[a[1]]|=2;
        at+=n;
    }
    if(opened||!first)goto done;
    for(size_t at=5;at<first;){const uint32_t *a=w+at;unsigned n=a[0]>>16,op=a[0]&65535;
        if(op==59&&n==4&&a[2]<bound&&set[a[2]]==1&&binding[a[2]]==3){
            if(variable||a[3]!=2||a[1]>=bound||!type[a[1]])goto done;
            const uint32_t *p=type[a[1]];
            if(p[0]!=(4u<<16|32)||p[2]!=2||p[3]>=bound||!type[p[3]])goto done;
            const uint32_t *s=type[p[3]];
            if(s[0]!=(3u<<16|30)||layout[p[3]]!=3||s[2]>=bound||!type[s[2]]||stride[s[2]]!=16)goto done;
            const uint32_t *ar=type[s[2]];
            if(ar[0]!=(4u<<16|28)||ar[3]>=bound||!known[ar[3]]||literal[ar[3]]!=4096||ar[2]>=bound||!type[ar[2]])goto done;
            const uint32_t *v=type[ar[2]];
            if(v[0]!=(4u<<16|23)||v[3]!=4||v[2]>=bound||!type[v[2]])goto done;
            const uint32_t *f=type[v[2]];if(f[0]!=(3u<<16|22)||f[2]!=32)goto done;
            variable=a[2];scalar=v[2];
        }at+=n;
    }
    if(!variable)goto done;
    for(size_t at=first;at<count;){const uint32_t *a=w+at;unsigned n=a[0]>>16,op=a[0]&65535;
        if((op==65||op==66)&&n==7&&a[3]==variable&&a[2]<bound&&a[4]<bound&&a[5]<bound&&a[6]<bound&&
           known[a[4]]&&known[a[5]]&&known[a[6]]&&!literal[a[4]]&&literal[a[5]]==1&&literal[a[6]]==3&&a[1]<bound&&type[a[1]]){
            const uint32_t *p=type[a[1]];if(p[0]==(4u<<16|32)&&p[2]==2&&p[3]==scalar)pointer[a[2]]=1;
        }
        if(op==61&&n>=4&&a[3]<bound&&pointer[a[3]]){if(n!=4||a[1]!=scalar)goto done;hits++;}
        at+=n;
    }
    if(hits){uint32_t *r=malloc((count+4)*4);if(!r)goto done;size_t dst=5;memcpy(r,w,20);r[3]=bound+1;
        for(size_t at=5;at<count;){const uint32_t *a=w+at;unsigned n=a[0]>>16,op=a[0]&65535;
            if(at==first){uint32_t c[]={4u<<16|43,scalar,bound,0};memcpy(r+dst,c,16);dst+=4;}
            memcpy(r+dst,a,n*4);if(op==61&&n==4&&a[1]==scalar&&a[3]<bound&&pointer[a[3]]){r[dst]=4u<<16|83;r[dst+3]=bound;}
            dst+=n;at+=n;
        }*out=r;*out_count=dst;
    }
done:
    free(type);free(set);free(binding);free(literal);free(stride);free(known);free(layout);free(pointer);return *out!=NULL;
}
