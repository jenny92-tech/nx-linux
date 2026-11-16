// INPUT: Two tightly packed compressed mip snapshots and their block geometry.
// OUTPUT: Changed block runs with tightly packed, aligned upload offsets.
// POS: CPU-only plan shared by Vulkan refresh and host regression tests.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t x,y,z,width,height;
    size_t offset;
} NvnCompressedRegion;
typedef struct {
    NvnCompressedRegion *regions;
    uint32_t count;
    size_t bytes;
} NvnCompressedPlan;

/* pixel_bytes=0 retains native compressed blocks; otherwise decode each run
 * into tightly packed texels. No previous decoded image is retained. */
static inline int nvn_compressed_plan(const uint8_t *old,const uint8_t *next,size_t length,
    uint32_t width,uint32_t height,uint32_t depth,unsigned bw,unsigned bh,unsigned block,
    unsigned pixel_bytes,NvnCompressedPlan *out) {
    *out=(NvnCompressedPlan){0};
    if(!old||!next||!width||!height||!depth||!bw||!bh||bw>12||bh>12||
       (block!=8&&block!=16)||pixel_bytes>8||(pixel_bytes&&(pixel_bytes&(pixel_bytes-1))))return -1;
    uint32_t columns=(width-1)/bw+1,rows=(height-1)/bh+1;
    uint64_t blocks=(uint64_t)columns*rows;
    if(blocks>SIZE_MAX/block/depth||length!=(size_t)blocks*block*depth)return -1;
    size_t row=(size_t)columns*block;
    unsigned alignment=pixel_bytes?(pixel_bytes>4?pixel_bytes:4):block;
    uint32_t capacity=0;
    for(uint32_t z=0;z<depth;z++)for(uint32_t y=0;y<rows;y++){
        size_t start=((size_t)z*rows+y)*row;
        uint32_t x=0;
        while(x<columns){
            if(!memcmp(old+start+(size_t)x*block,next+start+(size_t)x*block,block)){x++;continue;}
            uint32_t first=x++;
            while(x<columns&&memcmp(old+start+(size_t)x*block,next+start+(size_t)x*block,block))x++;
            uint32_t px=first*bw,py=y*bh;
            uint32_t w=width-px,h=height-py;
            if((uint64_t)w>(uint64_t)(x-first)*bw)w=(x-first)*bw;
            if(h>bh)h=bh;
            uint64_t bytes=pixel_bytes?(uint64_t)w*h*pixel_bytes:(uint64_t)(x-first)*block;
            if(out->bytes>SIZE_MAX-(alignment-1))goto fail;
            size_t offset=(out->bytes+alignment-1)&~(size_t)(alignment-1);
            if(bytes>SIZE_MAX-offset)goto fail;
            if(out->count==capacity){
                uint32_t cap=capacity?capacity*2:64;
                if(cap<capacity||(uint64_t)cap*sizeof(*out->regions)>SIZE_MAX)goto fail;
                void *p=realloc(out->regions,(size_t)cap*sizeof(*out->regions));
                if(!p)goto fail;
                out->regions=p;capacity=cap;
            }
            out->regions[out->count++]=(NvnCompressedRegion){px,py,z,w,h,offset};
            out->bytes=offset+(size_t)bytes;
        }
    }
    return 0;
fail:
    free(out->regions);*out=(NvnCompressedPlan){0};return -1;
}
