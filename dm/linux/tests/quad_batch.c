// INPUT: Synthetic distinct vertex attributes, bounded buffers and SPIR-V hazards.
// OUTPUT: Original ordered triangles, exact attribute bytes and conservative shader rejection.
// POS: Host sanitizer regression for optional UI batching; no game or GPU needed.
#include "../../gfx/nvn/nvn_vk_quad_batch.h"
#include <assert.h>
#include <stdio.h>
static uint32_t vertex(const uint8_t *p,unsigned i){uint32_t v;memcpy(&v,p+(size_t)i*20,4);return v;}
int main(void){
    uint8_t out[NVN_QUAD_BYTES+32],quad[80];memset(out,0xa5,sizeof out);unsigned count=0;
    for(unsigned q=0;q<NVN_QUAD_MAX;q++){
        for(unsigned v=0;v<4;v++)for(unsigned a=0;a<5;a++){
            uint32_t value=(q*4+v)*16+a;memcpy(quad+v*20+a*4,&value,4);
        }
        assert(nvn_quad_pack(out,NVN_QUAD_BYTES,&count,quad)&&count==q+1);
    }
    unsigned triangle=0,n=count*6-2;
    for(unsigned i=0;i+2<n;i++){
        uint32_t a=vertex(out,i)/16,b=vertex(out,i+1)/16,c=vertex(out,i+2)/16;
        if(a==b||a==c||b==c)continue;
        if(i&1){uint32_t t=a;a=b;b=t;}
        unsigned q=triangle/2;assert(a==q*4+(triangle%2?2:0));
        assert(b==q*4+1&&c==q*4+(triangle%2?3:2));triangle++;
    }
    assert(triangle==2*NVN_QUAD_MAX);
    uint8_t wide[(NVN_QUAD_MAX*6u-2u)*36u+32],source36[144];memset(wide,0xa5,sizeof wide);unsigned wide_count=0;
    for(unsigned q=0;q<NVN_QUAD_MAX;q++){
        for(unsigned v=0;v<4;v++)for(unsigned a=0;a<9;a++){uint32_t value=(q*4+v)*16+a;memcpy(source36+v*36+a*4,&value,4);}
        assert(nvn_quad_pack_stride(wide,sizeof wide-32,&wide_count,source36,36));
    }
    unsigned wide_triangles=0;
    for(unsigned i=0;i<wide_count*6u-2u;i++){
        uint32_t value;memcpy(&value,wide+(size_t)i*36,4);
        for(unsigned a=1;a<9;a++){uint32_t attribute;memcpy(&attribute,wide+(size_t)i*36+a*4,4);assert(attribute==value+a);}
        if(i+2>=wide_count*6u-2u)continue;
        uint32_t a=value/16,b,c;memcpy(&b,wide+(size_t)(i+1)*36,4);memcpy(&c,wide+(size_t)(i+2)*36,4);b/=16;c/=16;
        if(a==b||a==c||b==c)continue;if(i&1){uint32_t t=a;a=b;b=t;}
        unsigned q=wide_triangles/2;assert(a==q*4+(wide_triangles%2?2:0)&&b==q*4+1&&c==q*4+(wide_triangles%2?3:2));wide_triangles++;
    }
    assert(wide_triangles==512);
    for(unsigned i=sizeof wide-32;i<sizeof wide;i++)assert(wide[i]==0xa5);
    for(unsigned i=0;i<n;i++)for(unsigned a=1;a<5;a++){
        uint32_t value;memcpy(&value,out+(size_t)i*20+a*4,4);assert(value==vertex(out,i)+a);
    }
    assert(!nvn_quad_pack(out,NVN_QUAD_BYTES,&count,quad));
    for(unsigned i=NVN_QUAD_BYTES;i<sizeof out;i++)assert(out[i]==0xa5);
    count=0;assert(!nvn_quad_pack(out,79,&count,quad)&&!count);
    uint32_t shader[]={0x07230203,0x10000,0,100,0,0x20011,1,0x4000f,0,1,0,0x40047,2,11,0};
    assert(nvn_quad_shader_safe(shader,sizeof shader/4,0));
    uint32_t imported[24];memcpy(imported,shader,sizeof shader);
    const uint32_t glsl[]={0x6000b,3,0x4c534c47,0x6474732e,0x3035342e,0};
    memcpy(imported+15,glsl,sizeof glsl);
    assert(nvn_quad_shader_safe(imported,21,0));
    imported[20]=1;assert(!nvn_quad_shader_safe(imported,21,0));imported[20]=0;
    imported[21]=0x3002d;imported[22]=1;imported[23]=4;
    assert(!nvn_quad_shader_safe(imported,24,0));
    for(unsigned builtin=2;builtin<=50;builtin++){
        shader[14]=builtin;assert(!nvn_quad_shader_safe(shader,sizeof shader/4,0));
    }
    shader[14]=0;shader[6]=50;assert(nvn_quad_shader_safe(shader,sizeof shader/4,0));
    shader[6]=61;assert(!nvn_quad_shader_safe(shader,sizeof shader/4,0));shader[6]=1;
    shader[7]=0x9000f;assert(!nvn_quad_shader_safe(shader,sizeof shader/4,0));
    /* Mixed odd/even indexed strips must retain original triangle winding,
     * order, attributes and base-vertex semantics, including source degenerates. */
    uint8_t strips[65536],original[256*36],saved[65536];unsigned used=0,expected[8192][3],nt=0;
    for(unsigned i=0;i<256;i++)for(unsigned a=0;a<9;a++){
        uint32_t value=i*16+a;memcpy(original+(size_t)i*36+a*4,&value,4);
    }
    const unsigned lengths[]={3,4,5,22,65};
    for(unsigned q=0;q<20;q++){
        unsigned length=lengths[q%5],order[65];uint16_t small[65];uint32_t large[65];int base=(q&1)?-1:1;
        for(unsigned i=0;i<length;i++){
            order[i]=(i+q*7)%200+2;
            if(i>2&&i%11==0)order[i]=order[i-1];
            small[i]=(uint16_t)(order[i]-base);large[i]=order[i]-base;
        }
        for(unsigned i=0;i+2<length;i++){
            unsigned a=order[i],b=order[i+1],c=order[i+2];
            if(a==b||a==c||b==c)continue;if(i&1){unsigned t=a;a=b;b=t;}
            expected[nt][0]=a;expected[nt][1]=b;expected[nt++][2]=c;
        }
        assert(nvn_indexed_strip_pack(strips,sizeof strips,&used,original,sizeof original,36,
            q&1?(const void *)small:(const void *)large,length,q&1?2:4,base));
    }
    unsigned actual=0;
    for(unsigned i=0;i+2<used;i++){
        uint32_t a,b,c;memcpy(&a,strips+(size_t)i*36,4);memcpy(&b,strips+(size_t)(i+1)*36,4);memcpy(&c,strips+(size_t)(i+2)*36,4);a/=16;b/=16;c/=16;
        if(a==b||a==c||b==c)continue;if(i&1){unsigned t=a;a=b;b=t;}
        assert(actual<nt&&a==expected[actual][0]&&b==expected[actual][1]&&c==expected[actual][2]);actual++;
    }
    assert(actual==nt);
    for(unsigned i=0;i<used;i++)for(unsigned a=1;a<9;a++){
        uint32_t id,attribute;memcpy(&id,strips+(size_t)i*36,4);memcpy(&attribute,strips+(size_t)i*36+a*4,4);assert(attribute==id+a);
    }
    memcpy(saved,strips,sizeof strips);unsigned before=used;uint16_t bad[]={0,1,UINT16_MAX};
    assert(!nvn_indexed_strip_pack(strips,sizeof strips,&used,original,sizeof original,36,bad,3,2,0));
    bad[2]=256;assert(!nvn_indexed_strip_pack(strips,sizeof strips,&used,original,sizeof original,36,bad,3,2,0));
    bad[2]=2;assert(!nvn_indexed_strip_pack(strips,sizeof strips,&used,original,sizeof original,36,bad,3,2,-1));
    assert(!nvn_indexed_strip_pack(strips,(size_t)used*36+35,&used,original,sizeof original,36,bad,3,2,0));
    assert(used==before&&!memcmp(saved,strips,sizeof strips));
    puts("quad batch: 20/36-byte layouts, 1024 ordered triangles, all attribute bytes, bounds and shader guards passed");
    printf("indexed strips: %u ordered triangles, odd/even joins, u16/u32, base vertices, restart/bounds and atomic rejection passed\n",nt);
}
