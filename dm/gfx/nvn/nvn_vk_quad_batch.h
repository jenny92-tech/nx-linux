// INPUT: Validated SPIR-V and four 20-byte vertices in original strip order.
// OUTPUT: A conservative shader guard and identical quads joined by zero-area triangles.
// POS: CPU helpers for optional consecutive UI-quad batching; no shader rewriting.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <string.h>

#define NVN_QUAD_STRIDE 20u
#define NVN_QUAD_MAX 256u
#define NVN_QUAD_BYTES ((NVN_QUAD_MAX*6u-2u)*NVN_QUAD_STRIDE)

static inline int nvn_quad_shader_safe(const uint32_t *w,size_t words,unsigned stage){
    if(!w||words<5||w[0]!=0x07230203u||stage>1)return 0;
    unsigned entries=0;
    for(size_t i=5;i<words;){
        unsigned n=w[i]>>16,op=w[i]&65535u;
        if(!n||n>words-i)return 0;
        if(op==45)return 0; /* Undef must not create nondegenerate bridge vertices. */
        if(op==11&&(n!=6||memcmp(w+i+2,"GLSL.std.450",13)))return 0;
        if(op==15){if(n<4||w[i+1]!=(stage?4u:0u))return 0;entries++;}
        if(op==17){if(n!=2||(w[i+1]!=1&&w[i+1]!=50))return 0;} /* Shader/ImageQuery; no subgroup/interlock effects. */
        unsigned builtin=~0u;
        if(op==71&&n>=4&&w[i+2]==11)builtin=w[i+3];
        if(op==72&&n>=5&&w[i+3]==11)builtin=w[i+4];
        /* PointSize is ignored for the eligible filled triangle strips.
         * Reject vertex/instance/primitive IDs, clipping and sample/depth IDs. */
        if(builtin!=~0u&&builtin!=(stage?15u:0u)&&!(stage==0&&builtin==1))return 0;
        if(op==99||(op>=224&&op<=242)||(op>=259&&op<=366)||
           (op>=5364&&op<=5365))return 0;
        i+=n;
    }
    return entries==1;
}

/* Source is an independent 80-byte snapshot. Appending six vertices keeps
 * each quad's strip parity and provoking-vertex order; four bridge triangles
 * have repeated vertices and therefore no covered samples. */
static inline int nvn_quad_pack_stride(uint8_t *out,size_t capacity,unsigned *quads,const void *source,unsigned stride){
    if(!out||!quads||!source||*quads>=NVN_QUAD_MAX||!stride||stride>256)return 0;
    size_t used=*quads?((size_t)*quads*6u-2u)*stride:0;
    size_t add=(*quads?6u:4u)*stride;
    if(used>capacity||add>capacity-used)return 0;
    if(*quads){
        memcpy(out+used,out+used-stride,stride);
        memcpy(out+used+stride,source,stride);
        used+=2u*stride;
    }
    memcpy(out+used,source,4u*stride);(*quads)++;return 1;
}
static inline int nvn_quad_pack(uint8_t *out,size_t capacity,unsigned *quads,const void *source){
    return nvn_quad_pack_stride(out,capacity,quads,source,NVN_QUAD_STRIDE);
}

/* The validated width is two or four bytes. Constant-size memcpy preserves
 * unaligned input support and lets the compiler use native loads in the loops. */
static inline uint32_t nvn_indexed_strip_index(const void *indices,unsigned i,unsigned bytes){
    const uint8_t *at=(const uint8_t *)indices+(size_t)i*bytes;
    if(bytes==2){uint16_t index;memcpy(&index,at,2);return index;}
    uint32_t index;memcpy(&index,at,4);return index;
}

/* Expand a bounded readonly indexed strip into identical attribute bytes.
 * An odd preceding strip needs one extra repeated vertex to preserve winding
 * and provoking-vertex order. Restart sentinels are deliberately unsupported.
 * Validate every index and the entire append before writing any output. */
static inline int nvn_indexed_strip_pack(uint8_t *out,size_t capacity,unsigned *used,
        const void *vertices,size_t bytes,unsigned stride,const void *indices,
        unsigned count,unsigned index_bytes,int base_vertex){
    if(!out||!used||!vertices||!indices||!stride||stride>256||
       count<3||count>512||(index_bytes!=2&&index_bytes!=4)||bytes%stride)return 0;
    size_t before=(size_t)*used*stride;
    unsigned bridge=*used?2u+(*used&1u):0;
    if(*used>UINT_MAX-count-bridge)return 0;
    size_t added=(size_t)(count+bridge)*stride;
    if(before>capacity||added>capacity-before)return 0;
    for(unsigned i=0;i<count;i++){
        uint32_t index=nvn_indexed_strip_index(indices,i,index_bytes);
        if(index==(index_bytes==2?UINT16_MAX:UINT32_MAX))return 0;
        int64_t at=(int64_t)index+base_vertex;
        if(at<0||(uint64_t)at>=bytes/stride)return 0;
    }
    unsigned first=nvn_indexed_strip_index(indices,0,index_bytes);
    const uint8_t *start=(const uint8_t *)vertices+((int64_t)first+base_vertex)*stride;
    if(*used){
        const uint8_t *last=out+before-stride;
        for(unsigned i=0;i<bridge-1;i++){memcpy(out+before,last,stride);before+=stride;}
        memcpy(out+before,start,stride);before+=stride;
    }
    for(unsigned i=0;i<count;i++){
        uint32_t index=nvn_indexed_strip_index(indices,i,index_bytes);
        memcpy(out+before,(const uint8_t *)vertices+((int64_t)index+base_vertex)*stride,stride);before+=stride;
    }
    *used+=count+bridge;return 1;
}
