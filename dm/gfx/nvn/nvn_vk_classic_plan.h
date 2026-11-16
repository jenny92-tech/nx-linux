// INPUT: One complete immutable batch, checked shader interfaces and counter liveness.
// OUTPUT: Optional classic-only HD draw/clear/copy/light omissions; uncertainty retains the batch.
// POS: Quality experiment for the known 90447 classic compositor, not a general render graph.
#pragma once
#include "nvn_obj.h"
#include "nvn_copy_pitch.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    const char *key;unsigned present,effects,nread;
    uint64_t code_address,code_size,control_address;
    unsigned buffer_use[2],buffer_write[2],vertex_locations;
    struct { unsigned bank,slot,write; } read[64];
} NvnClassicShader;
typedef struct {
    uint16_t slot[512];unsigned n;const char *last_key;
    unsigned code_checks_enabled;uint64_t code_checks,code_hits;
    unsigned read_checks_enabled;uint64_t read_checks,read_hits;
    struct {const prog_t *program;unsigned stage,code_checked;NvnClassicShader facts;} entry[256];
} NvnClassicMemo;
typedef struct { uint64_t address,size; } NvnClassicRange;
typedef struct { NvnClassicRange range[128];unsigned n,hull_valid;uint64_t low,high; } NvnClassicRanges;
typedef struct {
    p_set_rt rt;const prog_t *program[6];
    uint64_t texture[6][128],image[6][32];
    const dpool_t *pool;
    NvnClassicRange buffer[2][6][16];
    NVNbufferRange vertex[16];va_st attribute[16];
    int64_t copy_stride,copy_image_stride;
    NvnClassicShader shader[6];uint16_t memo_index[6];unsigned valid,reads_valid,read_cache;
} NvnClassicState;
typedef struct {
    void *user;
    int (*shader)(void *,const prog_t *,unsigned,NvnClassicShader *);
    const tex_t *(*texture)(void *,uint64_t);
    size_t *position;
    int (*format_blocks)(void *,int,int *,int *);
    int freeze_hd_queries;
    NvnClassicMemo *memo;
    int freeze_hd_effects;
} NvnClassicReader;
/* Keep provenance: resuming the reviewed fluid simulation may read its own
 * paused images, but must never exempt an HD attachment or lighting output. */
typedef struct { uint64_t serial[512];uint8_t cause[512];unsigned n;int overflow; } NvnClassicTargets;
enum { NVN_CLASSIC_TARGET_HD=1,NVN_CLASSIC_TARGET_FLUID=2 };
enum { NVN_CLASSIC_NOT_SELECTED=0,NVN_CLASSIC_OK=1,NVN_CLASSIC_BAD_RECORD=2,
    NVN_CLASSIC_UNKNOWN_SHADER=3,NVN_CLASSIC_DEPENDENCY=4,NVN_CLASSIC_EFFECT=5,
    NVN_CLASSIC_LIVE_COUNTER=6,NVN_CLASSIC_ALIAS=7,NVN_CLASSIC_CAPACITY=8 };

static inline int nvn_classic_has(const NvnClassicTargets *t,uint64_t serial) {
    for(unsigned i=0;i<t->n;i++)if(t->serial[i]==serial)return 1;
    return 0;
}
static inline unsigned nvn_classic_cause(const NvnClassicTargets *t,uint64_t serial) {
    for(unsigned i=0;i<t->n;i++)if(t->serial[i]==serial)return t->cause[i];
    return 0;
}
static inline void nvn_classic_add_kind(NvnClassicTargets *t,const tex_t *image,unsigned cause) {
    if(!image||!image->serial||!cause)return;
    for(unsigned i=0;i<t->n;i++)if(t->serial[i]==image->serial){t->cause[i]|=cause;return;}
    if(t->n==512){t->overflow=1;return;}
    t->serial[t->n]=image->serial;t->cause[t->n++]=(uint8_t)cause;
}
static inline void nvn_classic_add(NvnClassicTargets *t,const tex_t *image) {
    nvn_classic_add_kind(t,image,NVN_CLASSIC_TARGET_HD);
}
static inline int nvn_classic_fluid(const NvnClassicShader *s) {
    return s->present&&s->key&&(!strcmp(s->key,"0c7adefa3a80bbcb")||
        !strcmp(s->key,"eb8a0d0872a77ef4")||!strcmp(s->key,"3f27364f7a346ee6"));
}
static inline int nvn_classic_draw(unsigned op) {
    return op==OP_DRAW||op==OP_DRAW_IDX||op==OP_DRAW_INSTANCED;
}
#ifdef DM_LINUX_GAME_94012
#define NVN_CLASSIC_TERRAIN_QUERY_KEY "45e7f08773cae312"
#else
#define NVN_CLASSIC_TERRAIN_QUERY_KEY "e9cc50cf2ce3e33e"
#endif
/* Offline inventory identifies these exact original 90447 programs as the HD
 * light-bin/depth-bounds/grid and terrain query. This is a title quality policy, not generic
 * dead-compute elimination. Every retained consumer is checked below. */
static inline int nvn_classic_hd_compute(const NvnClassicShader *s,int freeze_queries,int freeze_effects) {
    return s->present&&s->key&&(!strcmp(s->key,"30a35d4b6d77a433")||
        !strcmp(s->key,"1f2f8f466c29ac30")||!strcmp(s->key,"83365fcb02fc4a9a")||
        (freeze_queries&&!strcmp(s->key,NVN_CLASSIC_TERRAIN_QUERY_KEY))||
        (freeze_effects&&nvn_classic_fluid(s)));
}
static inline int nvn_classic_range_valid(NvnClassicRange r) {
    return (!r.address&&!r.size)||(r.address&&r.size&&r.size<=UINT64_MAX-r.address);
}
static inline int nvn_classic_range_overlap(NvnClassicRange a,NvnClassicRange b) {
    return a.size&&b.size&&(a.address<=b.address?b.address-a.address<a.size:a.address-b.address<b.size);
}
static inline int nvn_classic_range_add(NvnClassicRanges *out,NvnClassicRange r) {
    if(!nvn_classic_range_valid(r))return NVN_CLASSIC_BAD_RECORD;
    if(!r.size)return NVN_CLASSIC_OK;
    for(unsigned i=0;i<out->n;i++)if(out->range[i].address==r.address&&out->range[i].size==r.size)return NVN_CLASSIC_OK;
    if(out->n==128)return NVN_CLASSIC_CAPACITY;
    if(!out->n){out->low=r.address;out->high=r.address+r.size;out->hull_valid=1;}
    else if(out->hull_valid){
        if(r.address<out->low)out->low=r.address;
        if(r.address+r.size>out->high)out->high=r.address+r.size;
    }
    out->range[out->n++]=r;return NVN_CLASSIC_OK;
}
static inline int nvn_classic_range_reads(const NvnClassicRanges *out,NvnClassicRange r) {
    if(!nvn_classic_range_valid(r))return NVN_CLASSIC_BAD_RECORD;
    if(out->hull_valid&&(!r.size||r.address>=out->high||r.address+r.size<=out->low))return NVN_CLASSIC_OK;
    for(unsigned i=0;i<out->n;i++)if(nvn_classic_range_overlap(out->range[i],r))return NVN_CLASSIC_DEPENDENCY;
    return NVN_CLASSIC_OK;
}
static inline int nvn_classic_range_contains(const NvnClassicRanges *out,NvnClassicRange r) {
    if(!nvn_classic_range_valid(r)||!r.size)return 0;
    if(out->hull_valid&&(r.address<out->low||r.address+r.size>out->high))return 0;
    for(unsigned i=0;i<out->n;i++){
        NvnClassicRange q=out->range[i];
        if(r.address>=q.address&&r.address-q.address<=q.size&&r.size<=q.size-(r.address-q.address))return 1;
    }
    return 0;
}
static inline int nvn_classic_state(NvnClassicState *s,op_hdr h,const uint8_t *a) {
    if((h.op==OP_DRAW&&h.size<8+sizeof(p_draw))||
       (h.op==OP_DRAW_IDX&&h.size<8+sizeof(p_draw_idx))||
       (h.op==OP_DRAW_INSTANCED&&h.size<8+sizeof(p_draw_instanced)))return 0;
    if(h.op==OP_SET_RT){
        if(h.size<8+sizeof(p_set_rt))return 0;
        memcpy(&s->rt,a,sizeof s->rt);if(s->rt.n<0||s->rt.n>8)return 0;
    }else if(h.op==OP_BIND_PROGRAM){
        if(h.size<8+sizeof(p_bind_program))return 0;
        p_bind_program b;memcpy(&b,a,sizeof b);if(b.stages<0||(unsigned)b.stages&~63u)return 0;
        for(unsigned i=0;i<6;i++)if((b.stages&(1u<<i))&&(!s->read_cache||s->program[i]!=b.prog)){
            s->program[i]=b.prog;s->valid&=~(1u<<i);s->reads_valid&=~(1u<<i);
        }
    }else if(h.op==OP_BIND_TEX||h.op==OP_BIND_IMG){
        if(h.size<8+offsetof(p_bind_handles,h))return 0;
        const p_bind_handles *b=(const void *)a;int limit=h.op==OP_BIND_TEX?128:32;
        if(b->stage<0||b->stage>=6||b->first<0||b->n<0||b->n>32||b->first>limit||b->n>limit-b->first||
           h.size<8+offsetof(p_bind_handles,h)+(size_t)b->n*8)return 0;
        uint64_t *out=h.op==OP_BIND_TEX?s->texture[b->stage]:s->image[b->stage];
        if(s->reads_valid&(1u<<b->stage))for(unsigned i=0;i<(unsigned)b->n;i++)if(out[b->first+i]!=b->h[i]){
            unsigned used=!(s->valid&(1u<<b->stage));
            const NvnClassicShader *f=&s->shader[b->stage];
            if(f->nread>64)used=1;
            for(unsigned j=0;!used&&j<f->nread;j++)if(f->read[j].bank==(unsigned)(h.op==OP_BIND_IMG)&&f->read[j].slot==(unsigned)b->first+i)used=1;
            if(used){s->reads_valid&=~(1u<<b->stage);break;}
        }
        memcpy(out+b->first,b->h,(size_t)b->n*8);
    }else if(h.op==OP_BIND_UBO||h.op==OP_BIND_SSBO){
        if(h.size<8+sizeof(p_bind_buf))return 0;
        const p_bind_buf *b=(const void *)a;
        if(b->stage<0||b->stage>=6||b->index<0||b->index>=16)return 0;
        NvnClassicRange *out=&s->buffer[h.op==OP_BIND_SSBO][b->stage][b->index];
        if((out->address!=b->addr||out->size!=b->size)&&
           (!(s->valid&(1u<<b->stage))||s->shader[b->stage].buffer_use[h.op==OP_BIND_SSBO]&(1u<<b->index)))s->reads_valid&=~(1u<<b->stage);
        *out=(NvnClassicRange){b->addr,b->size};
    }else if(h.op==OP_BIND_VBUF||h.op==OP_BIND_VATTR){
        if(h.size<16)return 0;int first,n;memcpy(&first,a,4);memcpy(&n,a+4,4);
        size_t stride=h.op==OP_BIND_VBUF?sizeof(NVNbufferRange):sizeof(va_st);
        if(first<0||n<0||first>16||n>16-first||h.size<16+(size_t)n*stride)return 0;
        if(h.op==OP_BIND_VBUF)memcpy(s->vertex+first,a+8,(size_t)n*stride);
        else memcpy(s->attribute+first,a+8,(size_t)n*stride);
    }else if(h.op==OP_COPY_STRIDE||h.op==OP_COPY_IMAGE_STRIDE){
        if(h.size<8+sizeof(p_copy_stride))return 0;
        int64_t stride;memcpy(&stride,a,8);if(stride<0)return 0;
        if(h.op==OP_COPY_STRIDE)s->copy_stride=stride;else s->copy_image_stride=stride;
    }else if(h.op==OP_SET_TEX_POOL){
        if(h.size<8+sizeof(p_set_pool))return 0;
        const p_set_pool *p=(const void *)a;if(!p->pool||(s->pool&&p->pool!=s->pool))return 0;
        s->reads_valid=0;s->pool=p->pool;
    }
    return h.size>=8&&h.size%8==0&&h.op<OP_COUNT_;
}
static inline int nvn_classic_shader(NvnClassicState *s,unsigned stage,NvnClassicReader r) {
    /* Memo lifetime is ONE immutable recorded batch. First use still verifies
     * full live control/code identity; explicit buffer/image/compute writes to
     * any used shader code or control range reject the complete plan below.
     * Actual retained draw replay keeps its normal full-byte identity checks. */
    if(s->valid&(1u<<stage)){if(r.memo)r.memo->last_key=s->shader[stage].key;return 1;}
    if(r.memo){
        uint64_t key=((uint64_t)(uintptr_t)s->program[stage]>>4)^((uint64_t)stage*0x9e3779b97f4a7c15ull);
        unsigned slot=(unsigned)((key*0xbf58476d1ce4e5b9ull)>>32)&511;
        for(unsigned tries=0;tries<512;tries++,slot=(slot+1)&511){
            unsigned index=r.memo->slot[slot];
            if(!index){
                if(r.memo->n==256||!r.shader(r.user,s->program[stage],stage,&s->shader[stage]))return 0;
                unsigned at=r.memo->n++;r.memo->entry[at].program=s->program[stage];r.memo->entry[at].stage=stage;
                r.memo->entry[at].facts=s->shader[stage];r.memo->entry[at].code_checked=0;
                r.memo->slot[slot]=s->memo_index[stage]=(uint16_t)(at+1);break;
            }
            if(r.memo->entry[index-1].program==s->program[stage]&&r.memo->entry[index-1].stage==stage){
                s->shader[stage]=r.memo->entry[index-1].facts;s->memo_index[stage]=(uint16_t)index;break;
            }
            if(tries==511)return 0;
        }
        r.memo->last_key=s->shader[stage].key;
    }else if(!r.shader(r.user,s->program[stage],stage,&s->shader[stage]))return 0;
    s->valid|=1u<<stage;return 1;
}
/* The complete writes set is fixed BEFORE the second walk. Only cache a
 * successful code/control check for this batch's already verified shader
 * identity; binding/resource consumer checks still run at every draw. Entries
 * are initialized on first use and the caller resets the memo each batch. */
static inline int nvn_classic_shader_writes(NvnClassicState *s,unsigned stage,
    NvnClassicReader reader,const NvnClassicRanges *writes) {
    NvnClassicMemo *memo=reader.memo;unsigned index=s->memo_index[stage];
    const NvnClassicShader *f=&s->shader[stage];
    int cached=memo&&memo->code_checks_enabled&&index&&index<=memo->n&&
        memo->entry[index-1].program==s->program[stage]&&memo->entry[index-1].stage==stage&&
        memo->entry[index-1].facts.code_address==f->code_address&&
        memo->entry[index-1].facts.code_size==f->code_size&&
        memo->entry[index-1].facts.control_address==f->control_address;
    if(cached&&memo->entry[index-1].code_checked){memo->code_hits++;return NVN_CLASSIC_OK;}
    if(memo)memo->code_checks++;
    for(unsigned j=0;j<writes->n;j++)if(
        nvn_classic_range_overlap((NvnClassicRange){f->code_address,f->code_size},writes->range[j])||
        (f->control_address&&nvn_classic_range_overlap((NvnClassicRange){f->control_address,0x880},writes->range[j])))return NVN_CLASSIC_EFFECT;
    if(cached)memo->entry[index-1].code_checked=1;
    return NVN_CLASSIC_OK;
}
static inline int nvn_classic_overlap(const tex_t *a,const tex_t *b) {
    if(!a||!b||!a->mem||!b->mem)return 1;
    uintptr_t x=(uintptr_t)a->mem,y=(uintptr_t)b->mem;
    return x<=y?y-x<a->L.size:x-y<b->L.size;
}
static inline int nvn_classic_reads(NvnClassicState *s,unsigned stage,NvnClassicReader r,
    const NvnClassicTargets *targets,const NvnClassicTargets *roots,const NvnClassicRanges *outputs) {
    if(!nvn_classic_shader(s,stage,r))return NVN_CLASSIC_UNKNOWN_SHADER;
    /* The second walk has fixed complete outputs/roots/history and immutable
     * texture descriptors. Reuse only this stage's successful address checks
     * until a program/resource binding changes; vertex/index checks stay per
     * draw. Never carry this bit across batches or into the second walk. */
    if(r.memo&&r.memo->read_checks_enabled&&(s->reads_valid&(1u<<stage))){r.memo->read_hits++;return NVN_CLASSIC_OK;}
    if(r.memo)r.memo->read_checks++;
    const NvnClassicShader *f=&s->shader[stage];
    int result=nvn_classic_range_reads(outputs,(NvnClassicRange){f->code_address,f->code_size});
    if(result!=NVN_CLASSIC_OK)return result;
    for(unsigned bank=0;bank<2;bank++)for(unsigned slot=0;slot<16;slot++)if(f->buffer_use[bank]&(1u<<slot)){
        result=nvn_classic_range_reads(outputs,s->buffer[bank][stage][slot]);if(result!=NVN_CLASSIC_OK)return result;
    }
    for(unsigned i=0;i<f->nread;i++){
        unsigned slot=f->read[i].slot,bank=f->read[i].bank;
        if(bank>1||slot>=(bank?32u:128u))return NVN_CLASSIC_BAD_RECORD;
        uint64_t handle=bank?s->image[stage][slot]:s->texture[stage][slot];
        const tex_t *t=r.texture(r.user,handle);
        if(!t&&!handle)continue; /* Same null-binding rule as collect_image_uses. */
        if(!t)return NVN_CLASSIC_DEPENDENCY;
        result=nvn_classic_range_reads(outputs,(NvnClassicRange){(uint64_t)(uintptr_t)t->mem,t->L.size});
        if(result!=NVN_CLASSIC_OK)return result;
        if(nvn_classic_has(targets,t->serial)&&!nvn_classic_has(roots,t->serial))return NVN_CLASSIC_DEPENDENCY;
    }
    if(r.memo&&r.memo->read_checks_enabled)s->reads_valid|=1u<<stage;
    return NVN_CLASSIC_OK;
}
/* Target history also covers an offscreen image produced in an older batch.
 * A root/compute read of any such image retains all HD work conservatively.
 * Every batch must itself end in the exact classic compositor; split submits,
 * transitions, other games and menu compositors remain unchanged. */
static inline int nvn_classic_plan(const uint8_t *const *ops,size_t count,const uint8_t *counter_live,
    NvnClassicState initial,NvnClassicReader reader,NvnClassicTargets *history,uint8_t *omit) {
    if(!count)return NVN_CLASSIC_NOT_SELECTED;
    initial.reads_valid=0;
    initial.read_cache=reader.memo&&reader.memo->read_checks_enabled;
    NvnClassicState s=initial,last=initial;size_t last_draw=SIZE_MAX;
    /* Copy the large replay state once, rather than once per HD/classic draw. */
    for(size_t i=count;i>0;i--){op_hdr h;memcpy(&h,ops[i-1],8);if(nvn_classic_draw(h.op)){last_draw=i-1;break;}}
    NvnClassicRanges light_outputs={0},query_outputs={0};
    NvnClassicRanges writes={0};
    memset(omit,0,count);
    for(size_t i=0;i<count;i++){
        if(reader.position)*reader.position=i;
        op_hdr h;memcpy(&h,ops[i],8);const uint8_t *a=ops[i]+8;
        if(!nvn_classic_state(&s,h,a))return NVN_CLASSIC_BAD_RECORD;
        if(h.op==OP_SET_RT){
            for(int j=0;j<s.rt.n+(s.rt.depth?1:0);j++){
                const tex_t *t=j==s.rt.n?s.rt.depth:s.rt.color[j];nvn_classic_add(history,t);
                if(t){int result=nvn_classic_range_add(&writes,(NvnClassicRange){(uint64_t)(uintptr_t)t->mem,t->L.size});if(result!=NVN_CLASSIC_OK)return result;}
            }
        }
        if(h.op==OP_COPY_T2T){
            if(h.size<8+sizeof(p_copy_t2t))return NVN_CLASSIC_BAD_RECORD;
            const p_copy_t2t *c=(const void *)a;if(!c->src||!c->dst)return NVN_CLASSIC_BAD_RECORD;
            /* A copy-derived image must also be treated as a potential HD
             * dependency, including when a later copy or root reads it. */
            nvn_classic_add_kind(history,c->dst,nvn_classic_cause(history,c->src->serial));
            int result=nvn_classic_range_add(&writes,(NvnClassicRange){(uint64_t)(uintptr_t)c->dst->mem,c->dst->L.size});if(result!=NVN_CLASSIC_OK)return result;
        }
        if(h.op==OP_COPY_B2T||h.op==OP_CLEAR_TEX){
            const tex_t *t;
            if(h.op==OP_COPY_B2T){if(h.size<8+sizeof(p_copy_b2t))return NVN_CLASSIC_BAD_RECORD;t=((const p_copy_b2t *)a)->tex;}
            else {if(h.size<8+sizeof(p_clear_tex))return NVN_CLASSIC_BAD_RECORD;t=((const p_clear_tex *)a)->tex;}
            if(!t)return NVN_CLASSIC_BAD_RECORD;
            int result=nvn_classic_range_add(&writes,(NvnClassicRange){(uint64_t)(uintptr_t)t->mem,t->L.size});if(result!=NVN_CLASSIC_OK)return result;
        }
        if(h.op==OP_DISPATCH){
            if(h.size<8+sizeof(p_dispatch))return NVN_CLASSIC_BAD_RECORD;
            if(!nvn_classic_shader(&s,5,reader)||!s.shader[5].present)return NVN_CLASSIC_UNKNOWN_SHADER;
            const NvnClassicShader *compute=&s.shader[5];
            for(unsigned bank=0;bank<2;bank++)for(unsigned slot=0;slot<16;slot++)if(compute->buffer_write[bank]&(1u<<slot)){
                int result=nvn_classic_range_add(&writes,s.buffer[bank][5][slot]);if(result!=NVN_CLASSIC_OK)return result;
            }
            for(unsigned j=0;j<compute->nread;j++)if(compute->read[j].write){
                unsigned bank=compute->read[j].bank,slot=compute->read[j].slot;
                if(bank>1||slot>=(bank?32u:128u))return NVN_CLASSIC_BAD_RECORD;
                const tex_t *t=reader.texture(reader.user,bank?s.image[5][slot]:s.texture[5][slot]);if(!t)return NVN_CLASSIC_DEPENDENCY;
                int result=nvn_classic_range_add(&writes,(NvnClassicRange){(uint64_t)(uintptr_t)t->mem,t->L.size});if(result!=NVN_CLASSIC_OK)return result;
            }
            if(nvn_classic_hd_compute(&s.shader[5],reader.freeze_hd_queries,reader.freeze_hd_effects)){
                const NvnClassicShader *f=&s.shader[5];int result;
                for(unsigned bank=0;bank<2;bank++)for(unsigned slot=0;slot<16;slot++)if(f->buffer_write[bank]&(1u<<slot)){
                    result=nvn_classic_range_add(&light_outputs,s.buffer[bank][5][slot]);if(result!=NVN_CLASSIC_OK)return result;
                    if(!strcmp(f->key,NVN_CLASSIC_TERRAIN_QUERY_KEY)){
                        result=nvn_classic_range_add(&query_outputs,s.buffer[bank][5][slot]);if(result!=NVN_CLASSIC_OK)return result;
                    }
                }
                for(unsigned j=0;j<f->nread;j++)if(f->read[j].write){
                    unsigned bank=f->read[j].bank,slot=f->read[j].slot;
                    if(bank>1||slot>=(bank?32u:128u))return NVN_CLASSIC_BAD_RECORD;
                    uint64_t handle=bank?s.image[5][slot]:s.texture[5][slot];
                    const tex_t *t=reader.texture(reader.user,handle);if(!t)return NVN_CLASSIC_DEPENDENCY;
                    nvn_classic_add_kind(history,t,nvn_classic_fluid(f)?NVN_CLASSIC_TARGET_FLUID:NVN_CLASSIC_TARGET_HD);
                    result=nvn_classic_range_add(&light_outputs,(NvnClassicRange){(uint64_t)(uintptr_t)t->mem,t->L.size});
                    if(result!=NVN_CLASSIC_OK)return result;
                }
            }
        }
        if(h.op==OP_UPDATE_UB||h.op==OP_COPY_B2B||h.op==OP_SIGNAL_EVENT||h.op==OP_REPORT_COUNTER){
            uint64_t address,size;unsigned offset=h.op==OP_COPY_B2B?8:0;
            if(h.op==OP_REPORT_COUNTER)offset=8;
            if(h.size<8+offset+8)return NVN_CLASSIC_BAD_RECORD;
            memcpy(&address,a+offset,8);
            if(h.op==OP_UPDATE_UB||h.op==OP_COPY_B2B){
                if(h.size<8+offset+16)return NVN_CLASSIC_BAD_RECORD;memcpy(&size,a+offset+8,8);
            }else size=8;
            int result=nvn_classic_range_add(&writes,(NvnClassicRange){address,size});if(result!=NVN_CLASSIC_OK)return result;
        }
        if(i==last_draw)last=s;
    }
    if(reader.position)*reader.position=SIZE_MAX;
    if(last_draw==SIZE_MAX||!nvn_classic_shader(&last,1,reader))return NVN_CLASSIC_NOT_SELECTED;
    NvnClassicShader *final=&last.shader[1];
    if(!final->present||!final->key||strcmp(final->key,"e44ae4b6e36e3424"))return NVN_CLASSIC_NOT_SELECTED;
    if(history->overflow)return NVN_CLASSIC_CAPACITY;
    if(last.rt.n!=1||!last.rt.color[0]||last.rt.depth||final->effects||final->nread!=2)return NVN_CLASSIC_DEPENDENCY;
    const tex_t *root_image[3]={last.rt.color[0],NULL,NULL};NvnClassicTargets roots={0};
    nvn_classic_add(&roots,root_image[0]);
    for(unsigned i=0;i<2;i++){
        unsigned bank=final->read[i].bank,slot=final->read[i].slot;
        if(bank>1||slot>=(bank?32u:128u))return NVN_CLASSIC_BAD_RECORD;
        root_image[i+1]=reader.texture(reader.user,bank?last.image[1][slot]:last.texture[1][slot]);
        if(!root_image[i+1])return NVN_CLASSIC_DEPENDENCY;nvn_classic_add(&roots,root_image[i+1]);
    }
    if(roots.n!=3||!counter_live)return NVN_CLASSIC_DEPENDENCY;
    /* Separate opt-in quality experiment: retain copies of the existing HD
     * terrain-query values while stopping their updates. Taint destinations
     * too, so this never permits a retained GPU consumer to use frozen data.
     * CPU HD consumers may observe old query values; gameplay must be tested.
     * This is deliberately not described as API-equivalent dead-code removal. */
    if(query_outputs.n)for(size_t i=0;i<count;i++){
        op_hdr h;memcpy(&h,ops[i],8);if(h.op!=OP_COPY_B2B)continue;
        if(h.size<32)return NVN_CLASSIC_BAD_RECORD;
        uint64_t v[3];memcpy(v,ops[i]+8,sizeof v);
        if(nvn_classic_range_contains(&query_outputs,(NvnClassicRange){v[0],v[2]})){
            int result=nvn_classic_range_add(&query_outputs,(NvnClassicRange){v[1],v[2]});if(result!=NVN_CLASSIC_OK)return result;
            result=nvn_classic_range_add(&light_outputs,(NvnClassicRange){v[1],v[2]});if(result!=NVN_CLASSIC_OK)return result;
        }
    }
    for(unsigned k=0;k<3;k++){
        int result=nvn_classic_range_reads(&light_outputs,(NvnClassicRange){(uint64_t)(uintptr_t)root_image[k]->mem,root_image[k]->L.size});
        if(result!=NVN_CLASSIC_OK)return result;
    }
    /* A quality toggle resumes the exact known simulation from paused state.
     * Only those three programs may read fluid-only history. Root draws,
     * unknown compute and copies still see the complete dependency history.
     * Never clear history: it protects consumers across command batches. */
    NvnClassicTargets resumed={0};
    if(!reader.freeze_hd_effects)for(unsigned i=0;i<history->n;i++)if(history->cause[i]&NVN_CLASSIC_TARGET_HD){
        resumed.serial[resumed.n]=history->serial[i];resumed.cause[resumed.n++]=history->cause[i];
    }
    s=initial;
    for(size_t i=0;i<count;i++){
        if(reader.position)*reader.position=i;
        op_hdr h;memcpy(&h,ops[i],8);const uint8_t *a=ops[i]+8;
        if(!nvn_classic_state(&s,h,a))return NVN_CLASSIC_BAD_RECORD;
        if(h.op==OP_SET_RT){
            for(int j=0;j<s.rt.n+(s.rt.depth?1:0);j++){
                const tex_t *t=j==s.rt.n?s.rt.depth:s.rt.color[j];if(!t||nvn_classic_has(&roots,t->serial))continue;
                for(unsigned k=0;k<3;k++)if(nvn_classic_overlap(t,root_image[k]))return NVN_CLASSIC_ALIAS;
            }
        }
        if(nvn_classic_draw(h.op)){
            int root=0,other=0;
            for(int j=0;j<s.rt.n+(s.rt.depth?1:0);j++){
                const tex_t *t=j==s.rt.n?s.rt.depth:s.rt.color[j];if(!t)continue;
                if(nvn_classic_has(&roots,t->serial))root=1;else other=1;
            }
            if(!root&&!other)return NVN_CLASSIC_DEPENDENCY;
            if(root&&other)return NVN_CLASSIC_DEPENDENCY;
            for(unsigned stage=0;stage<2;stage++){
                if(!nvn_classic_shader(&s,stage,reader)||(!s.shader[stage].present&&stage==0))return NVN_CLASSIC_UNKNOWN_SHADER;
                if(nvn_classic_shader_writes(&s,stage,reader,&writes)!=NVN_CLASSIC_OK)return NVN_CLASSIC_EFFECT;
                if(root&&s.shader[stage].effects)return NVN_CLASSIC_EFFECT;
                if(root){int result=nvn_classic_reads(&s,stage,reader,history,&roots,&light_outputs);if(result!=NVN_CLASSIC_OK)return result;}
                else if(s.shader[stage].effects)return NVN_CLASSIC_EFFECT;
            }
            if(root){
                for(unsigned loc=0;loc<16;loc++)if(s.shader[0].vertex_locations&(1u<<loc)){
                    unsigned stream=s.attribute[loc].stream;if(stream>=16)return NVN_CLASSIC_BAD_RECORD;
                    NVNbufferRange b=s.vertex[stream];
                    int result=nvn_classic_range_reads(&light_outputs,(NvnClassicRange){b.address,b.size});if(result!=NVN_CLASSIC_OK)return result;
                }
                if(h.op==OP_DRAW_IDX){
                    const p_draw_idx *d=(const void *)a;
                    if(d->count<0||d->itype<0||d->itype>NVN_INDEX_TYPE_UNSIGNED_INT)return NVN_CLASSIC_BAD_RECORD;
                    unsigned bytes=d->itype==NVN_INDEX_TYPE_UNSIGNED_BYTE?1:d->itype==NVN_INDEX_TYPE_UNSIGNED_SHORT?2:4;
                    if(d->count){int result=nvn_classic_range_reads(&light_outputs,(NvnClassicRange){d->ibuf,(uint64_t)d->count*bytes});if(result!=NVN_CLASSIC_OK)return result;}
                }
            }
            if(!root){if(counter_live[i])return NVN_CLASSIC_LIVE_COUNTER;omit[i]=1;}
        }else if(h.op==OP_CLEAR_COLOR){
            if(h.size<8+sizeof(p_clear_color))return NVN_CLASSIC_BAD_RECORD;
            const p_clear_color *c=(const void *)a;
            if(c->index<0||c->index>=s.rt.n||!s.rt.color[c->index]||c->mask&~15)return NVN_CLASSIC_BAD_RECORD;
            if(!nvn_classic_has(&roots,s.rt.color[c->index]->serial))omit[i]=2;
        }else if(h.op==OP_CLEAR_DS){
            if(h.size<8+sizeof(p_clear_ds))return NVN_CLASSIC_BAD_RECORD;
            if(s.rt.depth&&!nvn_classic_has(&roots,s.rt.depth->serial))omit[i]=2;
        }else if(h.op==OP_DISPATCH){
            if(!nvn_classic_shader(&s,5,reader)||!s.shader[5].present)return NVN_CLASSIC_UNKNOWN_SHADER;
            const NvnClassicShader *f=&s.shader[5];
            if(nvn_classic_shader_writes(&s,5,reader,&writes)!=NVN_CLASSIC_OK)return NVN_CLASSIC_EFFECT;
            if(nvn_classic_hd_compute(f,reader.freeze_hd_queries,reader.freeze_hd_effects))omit[i]=4;
            else {const NvnClassicTargets *reads=nvn_classic_fluid(f)?&resumed:history;
                int result=nvn_classic_reads(&s,5,reader,reads,&roots,&light_outputs);if(result!=NVN_CLASSIC_OK)return result;}
        }else if(h.op==OP_COPY_T2B)return NVN_CLASSIC_DEPENDENCY;
        else if(h.op==OP_COPY_B2T&&light_outputs.n){
            if(h.size<8+sizeof(p_copy_b2t))return NVN_CLASSIC_BAD_RECORD;
            const p_copy_b2t *c=(const void *)a;
            if(!c->tex||!reader.format_blocks)return NVN_CLASSIC_DEPENDENCY;
            int bw=0,bh=0,bpb=reader.format_blocks(reader.user,c->tex->d.format,&bw,&bh);
            size_t row,image,size;
            if(c->flags||!nvn_copy_pitch(c->r.width,c->r.height,c->r.depth,bw,bh,bpb,
                s.copy_stride,s.copy_image_stride,&row,&image,&size))return NVN_CLASSIC_BAD_RECORD;
            int result=nvn_classic_range_reads(&light_outputs,(NvnClassicRange){c->src,size});if(result!=NVN_CLASSIC_OK)return result;
        }
        else if(h.op==OP_COPY_B2B||h.op==OP_WAIT_EVENT){
            size_t bytes=h.op==OP_COPY_B2B?24:16;if(h.size<8+bytes)return NVN_CLASSIC_BAD_RECORD;
            uint64_t address,size=4;memcpy(&address,a,8);if(h.op==OP_COPY_B2B)memcpy(&size,a+16,8);
            if(h.op==OP_COPY_B2B&&nvn_classic_range_contains(&query_outputs,(NvnClassicRange){address,size}))continue;
            int result=nvn_classic_range_reads(&light_outputs,(NvnClassicRange){address,size});if(result!=NVN_CLASSIC_OK)return result;
        }
        else if(h.op==OP_COPY_T2T){
            if(h.size<8+sizeof(p_copy_t2t))return NVN_CLASSIC_BAD_RECORD;
            const p_copy_t2t *c=(const void *)a;
            if(!c->src||!c->dst)return NVN_CLASSIC_BAD_RECORD;
            if(nvn_classic_has(history,c->src->serial)&&!nvn_classic_has(&roots,c->src->serial)){
                if(nvn_classic_has(&roots,c->dst->serial))return NVN_CLASSIC_DEPENDENCY;
                for(unsigned k=0;k<3;k++)if(nvn_classic_overlap(c->dst,root_image[k]))return NVN_CLASSIC_ALIAS;
                omit[i]=3;
            }
        }
    }
    return NVN_CLASSIC_OK;
}
