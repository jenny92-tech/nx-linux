// INPUT: Reviewed 90447 prepared main image and its original classic coefficient.
// OUTPUT: Validated conditional HD submission stubs, with a separate writable data page.
// POS: Shared computer patcher/runtime contract; no executable memory writes at runtime.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef DM_LINUX_GAME_94012
#include "native_hd_94012.h"
#else
#define DM_NATIVE_HD_GAME_BUILD "90447"
#define DM_NATIVE_HD_MAGIC "D2RHD904"
#define DM_NATIVE_HD_TOGGLE 0x33e750u
#define DM_NATIVE_HD_PANEL_FIRST 0x994e30u
#define DM_NATIVE_HD_PANEL_THIRD 0x994ec4u
#define DM_NATIVE_HD_PANEL_MANAGER 0x2d40ee8u
#define DM_NATIVE_HD_HUD_VTABLE 0x1f23d50u
#define DM_NATIVE_HD_BUILD "c5e0c2db39a8d8a3ae2d5dfaaa68e16e"
#define DM_NATIVE_HD_TEXT_END 0x19e4b20u
#define DM_NATIVE_HD_RO 0x19e5000u
#define DM_NATIVE_HD_DATA 0x1ea4000u
#define DM_NATIVE_HD_KEYS 0x306c000u
#define DM_NATIVE_HD_PATCH 0x3070000u
#define DM_NATIVE_HD_BLEND 0x2c0aca8u
#define DM_NATIVE_HD_SITES 8u
static const struct {uint32_t site,target,original,post[6],npost;} dm_native_hd_sites[DM_NATIVE_HD_SITES]={
    {0x14f3198,0x10dc8d0,0x97efa5ce,{0xa9447bfd,0x910143ff,0xd65f03c0},3},
    {0x14f397c,0x10e103c,0x97efb5b0,{0xa9477bfd,0xa9464ff4,0xa94557f6,0xa9445ff8,0x910203ff,0xd65f03c0},6},
    {0x1527c8c,0x10e0220,0x97eee165,{0xf9400268,0xf9400500,0xf8420c08,0xf9411108,0xd63f0100},5},
    {0x10a7038,0x15350d8,0x94123828,{0xf9401ffa,0x5280361b,0x360002b6,0xf9401be8,0xf9400100,0x97de4483},6},
    {0x152f7a4,0x15333b0,0x94000f03,{0x9000527e,0xd0005267,0xf9420fde,0xb0005266,0xf0005243,0xf940cce7},6},
    {0x14ed8b8,0x14eec50,0x940004e6,{0x1475e6ce,0xf9400fe3,0xa9421bfe,0xf94023e1,0xa94097e7,0xf9402be0},6},
    {0x72765c,0x14b67f4,0x94363c66,{0x08dffee8,0x36003ea8,0xf0012a80,0x91308000,0x9406c638,0x08dfff48},6},
    {0x727680,0x14bb11c,0x94364ea7,{0x08dfff48,0x36004108,0xf9455ac0,0x943651e4,0x08dffee8,0x360041c8},6}
};
static const uint32_t dm_native_hd_toggle[]={
    0xa9be4ff4,0xa9017bfd,0x910043fd,0xd0015008,0x913b6108,0x08dffd08,0x37000448,0xd0015014,0xf9477680,0xb4000200,
    0xb000c261,0x9115f821,0x941959ac,0xaa0003f3,0xf9477680,0xb4000080,0x9000c3a1,0x9128cc21,0x941959a6,0xb4000073,
    0x39414668,0x35000268,0xb4000060,0x39414408,0x35000208,0x90014668,0x9132b108,0x2d400500,0x1e212000,0x54000161,
    0x90014668,0x9132a108,0xbd400100,0x1e202008,0x1e2e1000,0x1e2703e1,0x1e20dc22,0x1e21dc00,0x2d008102,0xb9000d1f,
    0xa9417bfd,0xa8c24ff4,0xd65f03c0
};
/* The fourth site updates only the HD Granny model-instance poses/timeline.
 * Freeze the whole update so its own later blend/world-pose work cannot consume
 * a partially skipped pose. CPU side effects are a separately selected policy. */
static const uint32_t dm_native_hd_pose_entry[]={0xfc1a0fe8,0xa90167fa,0xa9025ff8,0xa90357f6,
    0xa9044ff4,0xa9057bfd,0x910143fd,0xf9401808};
/* Separate opt-in quality probes: these two object tasks prepare render
 * constants. In the measured classic camp, their matrix outputs went to
 * DMA-BUF while inputs remained ordinary CPU memory. Freeze the complete
 * preparation call, not isolated stores inside it. CPU consumers/other scenes
 * still require gameplay validation; this is not an equivalence transform. */
static const uint32_t dm_native_hd_render_entry[2][8]={
    {0xd10743ff,0xa9195ffc,0xa91a57f6,0xa91b4ff4,0xa91c7bfd,0x910703fd,0xb0003549,0x3dc06d20},
    {0xd10743ff,0xf900cbfc,0xa91a57f6,0xa91b4ff4,0xa91c7bfd,0x910703fd,0xa940d033,0xaa0003f6}
};
/* Separate world-update quality probes. The complete final render dispatcher
 * (main+0x14bd674) MUST remain: sampled modern HUD draws are its descendants.
 * These earlier calls return no consumed value at the reviewed sites. Their
 * CPU side effects require independent movement/combat/scene validation. */
static const uint32_t dm_native_hd_world_entry[2][8]={
    {0xd10343ff,0xfd0033e8,0xa9076ffc,0xa90867fa,0xa9095ff8,0xa90a57f6,0xa90b4ff4,0xa90c7bfd},
    {0xd10243ff,0xa90467fa,0xa9055ff8,0xa90657f6,0xa9074ff4,0xa9087bfd,0x910203fd,0xb000d468}
};
#endif
#define DM_NATIVE_HD_PAGE 0x4000u
#define DM_NATIVE_HD_TRAILER 256u
#define DM_NATIVE_HD_MASK_MAX ((1u<<DM_NATIVE_HD_SITES)-1u)
#define DM_NATIVE_HD_STUB_BYTES 48u
#define DM_NATIVE_HD_MAX_BYTES (64u*1024u*1024u)
static inline uint32_t dm_hd_read32(const uint8_t *p){uint32_t v;memcpy(&v,p,4);return v;}
static inline uint64_t dm_hd_read64(const uint8_t *p){uint64_t v;memcpy(&v,p,8);return v;}
static inline void dm_hd_write32(uint8_t *p,uint32_t v){memcpy(p,&v,4);}
static inline void dm_hd_write64(uint8_t *p,uint64_t v){memcpy(p,&v,8);}
static inline uint32_t dm_hd_branch(uint32_t pc,uint32_t target,int link){
    int64_t delta=(int64_t)target-pc;return (link?0x94000000u:0x14000000u)|((uint32_t)(delta/4)&0x3ffffffu);
}
static inline uint32_t dm_hd_adrp(unsigned reg,uint32_t pc,uint32_t target){
    int64_t pages=((int64_t)(target&~4095u)-(pc&~4095u))/4096;
    return 0x90000000u|(((uint32_t)pages&3u)<<29)|((((uint32_t)pages>>2)&0x7ffffu)<<5)|reg;
}
/* No flag-setting or FP instructions; x16/x17 and SP are restored before the
 * original call. Its return value is ignored by the reviewed caller/dispatcher.
 * Hardware LR differs during the opt-in wrapper; exception/debug equivalence
 * is not claimed. Mask zero and every nonzero blend word execute the call. */
static inline void dm_native_hd_stub(uint32_t out[12],uint32_t at,unsigned index,uint32_t control){
    uint32_t resume=dm_native_hd_sites[index].site+4;
    out[0]=0xa9bf47f0u;
    out[1]=dm_hd_adrp(16,at+4,control);
    out[2]=0xb9400000u|(16u<<5)|17u;
    out[3]=0x36000000u|(index<<19)|(6u<<5)|17u;
    out[4]=dm_hd_adrp(16,at+16,DM_NATIVE_HD_BLEND);
    out[5]=0xb9400000u|(((DM_NATIVE_HD_BLEND&4095u)/4)<<10)|(16u<<5)|17u;
    out[6]=0x35000000u|(3u<<5)|17u;
    out[7]=0xa8c147f0u;out[8]=dm_hd_branch(at+32,resume,0);
    out[9]=0xa8c147f0u;out[10]=dm_hd_branch(at+40,dm_native_hd_sites[index].target,1);
    out[11]=dm_hd_branch(at+44,resume,0);
}
static inline int dm_native_hd_common(const uint8_t *image,size_t bytes){
    if(bytes<DM_NATIVE_HD_PATCH||bytes>DM_NATIVE_HD_MAX_BYTES||bytes%4)return 0;
#ifdef DM_LINUX_GAME_94012
    for(unsigned i=0;i<DM_NATIVE_HD_SITES;i++)
        if(memcmp(image+dm_native_hd_sites[i].target,dm_native_hd_target_entry[i],32))return 0;
    if(memcmp(image+DM_NATIVE_HD_TOGGLE,dm_native_hd_toggle,sizeof dm_native_hd_toggle))return 0;
#else
    if(memcmp(image+0x15350d8,dm_native_hd_pose_entry,sizeof dm_native_hd_pose_entry))return 0;
    if(memcmp(image+0x15333b0,dm_native_hd_render_entry[0],32)||memcmp(image+0x14eec50,dm_native_hd_render_entry[1],32))return 0;
    if(memcmp(image+0x14b67f4,dm_native_hd_world_entry[0],32)||memcmp(image+0x14bb11c,dm_native_hd_world_entry[1],32))return 0;
    if(memcmp(image+DM_NATIVE_HD_TOGGLE,dm_native_hd_toggle,sizeof dm_native_hd_toggle))return 0;
#endif
    for(unsigned i=0;i<DM_NATIVE_HD_SITES;i++)
        if(memcmp(image+dm_native_hd_sites[i].site+4,dm_native_hd_sites[i].post,dm_native_hd_sites[i].npost*4))return 0;
    return 1;
}
/* An immutable trailer precedes the final dedicated RW/NX page. The existing
 * key page is deliberately untouched: install_key_page zeroes and seals it. */
static inline uint32_t dm_native_hd_control(const uint8_t *image,size_t bytes){
    if(bytes<2*DM_NATIVE_HD_PAGE||bytes>DM_NATIVE_HD_MAX_BYTES||bytes%DM_NATIVE_HD_PAGE)return 0;
    uint32_t control=(uint32_t)bytes-DM_NATIVE_HD_PAGE,header=control-DM_NATIVE_HD_TRAILER;
    if(memcmp(image+header,DM_NATIVE_HD_MAGIC,8)||dm_hd_read32(image+header+8)!=6||
       dm_hd_read32(image+header+12)!=DM_NATIVE_HD_SITES||dm_hd_read32(image+header+24)!=control)return 0;
    return control;
}
static inline int dm_native_hd_validate(const uint8_t *image,size_t bytes){
    uint32_t control=dm_native_hd_control(image,bytes);if(!control||!dm_native_hd_common(image,bytes))return 0;
    uint32_t header=control-DM_NATIVE_HD_TRAILER,records=header+32;
    if(dm_hd_read32(image+control)>DM_NATIVE_HD_MASK_MAX)return 0;
    uint64_t source=dm_hd_read64(image+header+16);
    if(source<DM_NATIVE_HD_PATCH||source>DM_NATIVE_HD_MAX_BYTES-3*DM_NATIVE_HD_PAGE||source%4)return 0;
    uint64_t first=(source+15)&~15ull,end=first+DM_NATIVE_HD_SITES*DM_NATIVE_HD_STUB_BYTES;
    if(end>header||((end+DM_NATIVE_HD_TRAILER+DM_NATIVE_HD_PAGE-1)&~(uint64_t)(DM_NATIVE_HD_PAGE-1))!=control)return 0;
    for(unsigned i=0;i<DM_NATIVE_HD_SITES;i++){
        const uint8_t *r=image+records+i*16;uint32_t at=(uint32_t)first+i*DM_NATIVE_HD_STUB_BYTES,words[12];
        if(dm_hd_read32(r)!=dm_native_hd_sites[i].site||dm_hd_read32(r+4)!=at||
           dm_hd_read32(r+8)!=dm_native_hd_sites[i].original||dm_hd_read32(r+12)!=(1u<<i)||
           dm_hd_read32(image+dm_native_hd_sites[i].site)!=dm_hd_branch(dm_native_hd_sites[i].site,at,0))return 0;
        dm_native_hd_stub(words,at,i,control);if(memcmp(image+at,words,sizeof words))return 0;
    }
    return 1;
}
static inline int dm_native_hd_prepare(uint8_t *image,size_t source,size_t capacity,size_t *output){
    if(!dm_native_hd_common(image,source)||source>DM_NATIVE_HD_MAX_BYTES-3*DM_NATIVE_HD_PAGE)return 0;
    for(unsigned i=0;i<DM_NATIVE_HD_SITES;i++)if(dm_hd_read32(image+dm_native_hd_sites[i].site)!=dm_native_hd_sites[i].original)return 0;
    size_t first=(source+15)&~(size_t)15,stub_end=first+DM_NATIVE_HD_SITES*DM_NATIVE_HD_STUB_BYTES;
    size_t control=(stub_end+DM_NATIVE_HD_TRAILER+DM_NATIVE_HD_PAGE-1)&~(size_t)(DM_NATIVE_HD_PAGE-1),end=control+DM_NATIVE_HD_PAGE;
    if(end>capacity||end>DM_NATIVE_HD_MAX_BYTES)return 0;
    size_t header=control-DM_NATIVE_HD_TRAILER,records=header+32;
    memset(image+source,0,end-source);memcpy(image+header,DM_NATIVE_HD_MAGIC,8);
    dm_hd_write32(image+header+8,6);dm_hd_write32(image+header+12,DM_NATIVE_HD_SITES);
    dm_hd_write64(image+header+16,source);dm_hd_write32(image+header+24,(uint32_t)control);
    for(unsigned i=0;i<DM_NATIVE_HD_SITES;i++){
        uint32_t at=(uint32_t)first+i*DM_NATIVE_HD_STUB_BYTES,words[12];uint8_t *r=image+records+i*16;
        dm_hd_write32(r,dm_native_hd_sites[i].site);dm_hd_write32(r+4,at);dm_hd_write32(r+8,dm_native_hd_sites[i].original);dm_hd_write32(r+12,1u<<i);
        dm_native_hd_stub(words,at,i,(uint32_t)control);memcpy(image+at,words,sizeof words);
        dm_hd_write32(image+dm_native_hd_sites[i].site,dm_hd_branch(dm_native_hd_sites[i].site,at,0));
    }
    *output=end;return dm_native_hd_validate(image,end);
}
