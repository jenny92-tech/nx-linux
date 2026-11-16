// INPUT: Deterministic BC/ASTC blocks, including sparse edits and edge mips.
// OUTPUT: Bit-exact partial decode/copy checks against independent full decodes.
// POS: Host-only regression; no graphics driver, guest image or device required.
#include "../../gfx/nvn/nvn_compressed_update.h"
#include "../../gfx/tex/nvn_bcdec.h"
#include <stdio.h>

static unsigned checks;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"compressed update failed line %d: %s\n",__LINE__,#x);exit(1);}} while(0)
static uint32_t rng=0x92ea71;
static uint8_t byte(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return (uint8_t)rng;}
static unsigned bpb(unsigned f){return f==0x41||f==0x42||f==0x45||f==0x46||f==0x49||f==0x4a?8:16;}

static void decoded_case(unsigned fmt,unsigned w,unsigned h,unsigned depth,unsigned pattern){
    uint32_t pf;int pixel;
    CHECK(nvn_bc_unpacked(fmt,&pf,&pixel));
    unsigned bb=bpb(fmt),columns=(w+3)/4,rows=(h+3)/4;
    size_t n=(size_t)columns*rows*depth*bb,plane=(size_t)w*h*pixel,total=plane*depth;
    uint8_t *old=malloc(n),*next=malloc(n),*full=malloc(total),*gpu=malloc(total),*want=malloc(total);
    CHECK(old&&next&&full&&gpu&&want);
    for(size_t i=0;i<n;i++)old[i]=byte();
    memcpy(next,old,n);
    for(size_t b=0;b<n/bb;b++)if(pattern==1||(pattern==2&&b%7==0)||(pattern==3&&b==n/bb-1))next[b*bb]^=0x5b;
    for(unsigned z=0;z<depth;z++)CHECK(!nvn_bc_decode(fmt,next+(size_t)z*columns*rows*bb,(size_t)columns*bb,w,h,full+z*plane,(size_t)w*pixel));
    /* Distinct GPU-authored contents must survive in every untouched block. */
    for(size_t i=0;i<total;i++)gpu[i]=want[i]=(uint8_t)(0xa5^(i*13));
    for(unsigned z=0;z<depth;z++)for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
        size_t b=((size_t)z*rows+y/4)*columns+x/4,p=((size_t)z*w*h+(size_t)y*w+x)*pixel;
        if(memcmp(old+b*bb,next+b*bb,bb))memcpy(want+p,full+p,pixel);
    }
    NvnCompressedPlan plan;CHECK(!nvn_compressed_plan(old,next,n,w,h,depth,4,4,bb,(unsigned)pixel,&plan));
    uint8_t *upload=malloc(plan.bytes+32);CHECK(upload);memset(upload,0xcc,plan.bytes+32);
    size_t covered=0,changed=0;
    for(size_t b=0;b<n/bb;b++)changed+=memcmp(old+b*bb,next+b*bb,bb)!=0;
    for(unsigned i=0;i<plan.count;i++){
        NvnCompressedRegion r=plan.regions[i];
        CHECK(r.x%4==0&&r.y%4==0&&r.z<depth&&r.x+r.width<=w&&r.y+r.height<=h);
        CHECK(r.offset%4==0&&r.offset+((size_t)r.width*r.height*pixel)<=plan.bytes);
        covered+=(r.width+3)/4;
        size_t at=(((size_t)r.z*rows+r.y/4)*columns+r.x/4)*bb;
        CHECK(!nvn_bc_decode(fmt,next+at,(size_t)columns*bb,r.width,r.height,upload+r.offset,(size_t)r.width*pixel));
        for(unsigned y=0;y<r.height;y++)memcpy(gpu+((size_t)r.z*w*h+(size_t)(r.y+y)*w+r.x)*pixel,
            upload+r.offset+(size_t)y*r.width*pixel,(size_t)r.width*pixel);
    }
    CHECK(covered==changed);
    CHECK(!memcmp(gpu,want,total));
    for(unsigned i=0;i<32;i++)CHECK(upload[plan.bytes+i]==0xcc);
    if(pattern==0)CHECK(!plan.count&&!plan.bytes&&!plan.regions);
    free(upload);free(plan.regions);free(old);free(next);free(full);free(gpu);free(want);
}

static void native_case(unsigned bw,unsigned bh,unsigned w,unsigned h,unsigned depth){
    unsigned cols=(w+bw-1)/bw,rows=(h+bh-1)/bh;
    size_t n=(size_t)cols*rows*depth*16;
    uint8_t *old=malloc(n),*next=malloc(n),*gpu=malloc(n),*want=malloc(n);CHECK(old&&next&&gpu&&want);
    for(size_t i=0;i<n;i++)old[i]=byte();
    memcpy(next,old,n);memset(gpu,0x6b,n);memset(want,0x6b,n);
    for(size_t b=0;b<n/16;b++)if(b%3==0||b==n/16-1){next[b*16]^=0x71;memcpy(want+b*16,next+b*16,16);}
    NvnCompressedPlan plan;CHECK(!nvn_compressed_plan(old,next,n,w,h,depth,bw,bh,16,0,&plan));
    uint8_t *upload=malloc(plan.bytes+16);CHECK(upload);memset(upload,0xab,plan.bytes+16);
    for(unsigned i=0;i<plan.count;i++){
        NvnCompressedRegion r=plan.regions[i];size_t bytes=(size_t)((r.width+bw-1)/bw)*16;
        CHECK(r.offset%16==0&&r.offset+bytes<=plan.bytes&&r.height<=bh);
        size_t at=(((size_t)r.z*rows+r.y/bh)*cols+r.x/bw)*16;
        memcpy(upload+r.offset,next+at,bytes);memcpy(gpu+at,upload+r.offset,bytes);
    }
    CHECK(!memcmp(gpu,want,n));
    for(unsigned i=0;i<16;i++)CHECK(upload[plan.bytes+i]==0xab);
    free(plan.regions);free(upload);free(old);free(next);free(gpu);free(want);
}

int main(void){
    const unsigned sizes[][3]={{1,1,1},{3,2,3},{7,5,1},{17,13,6},{64,32,2},{256,256,1}};
    for(unsigned fmt=0x41;fmt<=0x50;fmt++)for(unsigned i=0;i<sizeof sizes/sizeof sizes[0];i++)for(unsigned p=0;p<4;p++)
        decoded_case(fmt,sizes[i][0],sizes[i][1],sizes[i][2],p);
    const unsigned geometry[][2]={{4,4},{5,4},{8,8},{12,12}};
    for(unsigned i=0;i<4;i++)for(unsigned j=0;j<6;j++)native_case(geometry[i][0],geometry[i][1],sizes[j][0],sizes[j][1],sizes[j][2]);
    uint8_t old[65536]={0},next[65536]={0};next[0]=1;NvnCompressedPlan plan;
    CHECK(!nvn_compressed_plan(old,next,sizeof old,256,256,1,4,4,16,4,&plan));CHECK(plan.count==1&&plan.bytes==64);free(plan.regions);
    CHECK(!nvn_compressed_plan(old,next,sizeof old,256,256,1,4,4,16,8,&plan));CHECK(plan.bytes==128);free(plan.regions);
    CHECK(!nvn_compressed_plan(old,next,16384,256,256,1,8,8,16,0,&plan));CHECK(plan.bytes==16);free(plan.regions);
    CHECK(nvn_compressed_plan(old,next,15,1,1,1,4,4,16,4,&plan));
    CHECK(nvn_compressed_plan(old,next,16,0,1,1,4,4,16,4,&plan));
    CHECK(nvn_compressed_plan(NULL,next,16,1,1,1,4,4,16,4,&plan));
    CHECK(nvn_compressed_plan(old,next,16,1,1,1,4,4,16,3,&plan));
    CHECK(nvn_compressed_plan(old,next,16,UINT32_MAX,UINT32_MAX,UINT32_MAX,4,4,16,4,&plan));
    printf("COMPRESSED_UPDATE_OK checks=%u formats=16 native_geometry=4 sparse_RGBA8_bytes=64 sparse_RGBA16F_bytes=128 sparse_ASTC8_bytes=16\n",checks);
    return 0;
}
