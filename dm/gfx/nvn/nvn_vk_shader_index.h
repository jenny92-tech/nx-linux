#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "nvn_vk_bytes.h"

/* Live callers provide at least the 0x24-byte header. Validate it before
 * using version-specific metadata; only the proven declared size is identity. */
static inline size_t nvn_shader_control_identity_bytes(const void *control) {
    if(!control)return 0;
    const uint8_t *p=control;uint32_t magic,major,minor,declared;
    memcpy(&magic,p,4);memcpy(&major,p+4,4);memcpy(&minor,p+8,4);memcpy(&declared,p+0x20,4);
    if(magic!=0x98761234 || major!=1)return 0;
    return minor==14 && declared==0x848 ? 0x848 : minor==15 && declared==0x850 ? 0x850 : minor==16 && declared==0x880 ? 0x880 : 0;
}

typedef struct {
    const uint8_t *entry,*control,*code;
    uint32_t stage,code_size;
    size_t control_bytes;
} nvn_shader_index_record;

/* The cursor is published only after the complete record passes validation.
 * v1 is the existing D2R control-only format; v2 compares complete code too. */
static inline int nvn_shader_index_read(const uint8_t *data,size_t bytes,uint32_t version,
                                      size_t *cursor,nvn_shader_index_record *out) {
    if(version!=1 && version!=2)return 0;
    size_t header=version==2?24:20;
    if(*cursor>bytes || bytes-*cursor<header+0x880)return 0;
    nvn_shader_index_record record={.entry=data+*cursor,.control=data+*cursor+header,.control_bytes=0x880};
    memcpy(&record.stage,record.entry+16,4);
    if(record.stage!=0 && record.stage!=1 && record.stage!=5)return 0;
    if(version==2) {
        uint32_t declared,control_stage;
        record.control_bytes=nvn_shader_control_identity_bytes(record.control);
        if(!record.control_bytes)return 0;
        for(size_t i=record.control_bytes;i<0x880;i++)if(record.control[i])return 0;
        memcpy(&record.code_size,record.entry+20,4);memcpy(&declared,record.control+0x704,4);
        memcpy(&control_stage,record.control+0x714,4);
        if(control_stage!=record.stage ||
           !record.code_size || record.code_size!=declared || record.code_size>16*1024*1024 ||
           record.code_size>bytes-*cursor-header-0x880)return 0;
        record.code=record.control+0x880;
    }
    *cursor+=header+0x880+record.code_size;*out=record;return 1;
}

static inline int nvn_shader_index_equal(const nvn_shader_index_record *a,const nvn_shader_index_record *b) {
    return a->stage==b->stage && a->code_size==b->code_size && a->control_bytes==b->control_bytes &&
           !memcmp(a->control,b->control,a->control_bytes) &&
           (!a->code_size || !memcmp(a->code,b->code,a->code_size));
}

/* A memoized v2 entry is only a candidate. Recheck every live control/code
 * byte, including code length and stage; pointer reuse never proves identity.
 * entry belongs to the already validated, immutable process-lifetime index. */
static inline int nvn_shader_index_matches_live_mode(const uint8_t *entry,const void *control,
                                               uint32_t stage,const void *code,size_t bytes,int vector) {
    if(!entry || !control || !code)return 0;
    uint32_t stored_stage,stored_size;
    memcpy(&stored_stage,entry+16,4);memcpy(&stored_size,entry+20,4);
    size_t length=nvn_shader_control_identity_bytes(control);
    if(!length || length!=nvn_shader_control_identity_bytes(entry+24) || stored_stage!=stage || stored_size!=bytes)return 0;
    return vector ? nvn_bytes_equal(entry+24,control,length) && nvn_bytes_equal(entry+24+0x880,code,bytes) :
           !memcmp(entry+24,control,length) && !memcmp(entry+24+0x880,code,bytes);
}
static inline int nvn_shader_index_matches_live(const uint8_t *entry,const void *control,
                                               uint32_t stage,const void *code,size_t bytes) {
    return nvn_shader_index_matches_live_mode(entry,control,stage,code,bytes,0);
}
