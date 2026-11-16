// INPUT: Verified 94012 main and reviewed graphics-only submission sites.
// OUTPUT: Build-specific addresses and immutable instruction guards.
// POS: Included by native_hd_policy.h only for the 94012 runtime/patcher.
#pragma once
#define DM_NATIVE_HD_BUILD "db0507f36b84200ac05f75d76ed5026f"
#define DM_NATIVE_HD_GAME_BUILD "94012"
#define DM_NATIVE_HD_MAGIC "D2RHD940"
#define DM_NATIVE_HD_TEXT_END 0x1b786a0u
#define DM_NATIVE_HD_RO 0x1b79000u
#define DM_NATIVE_HD_DATA 0x2056000u
#define DM_NATIVE_HD_KEYS 0x3214000u
#define DM_NATIVE_HD_PATCH 0x3220000u
#define DM_NATIVE_HD_BLEND 0x2dc0758u
#define DM_NATIVE_HD_TOGGLE 0xb15d60u
#define DM_NATIVE_HD_PANEL_FIRST 0xb1cb80u
#define DM_NATIVE_HD_PANEL_THIRD 0xb1cc14u
#define DM_NATIVE_HD_PANEL_MANAGER 0x2f05458u
#define DM_NATIVE_HD_HUD_VTABLE 0x20c7668u
#define DM_NATIVE_HD_SITES 3u
static const struct {uint32_t site,target,original,post[6],npost;} dm_native_hd_sites[DM_NATIVE_HD_SITES]={
    {0x166d460,0x124a060,0x97ef7300,{0xa9447bfd,0x910143ff,0xd65f03c0},3},
    {0x166dc44,0x124e86c,0x97ef830a,{0xa9477bfd,0xa9464ff4,0xa94557f6,0xa9445ff8,0x910203ff,0xd65f03c0},6},
    {0x169908c,0x124da40,0x97eed26d,{0xf9400268,0xf9400500,0xf8420c08,0xf9411108,0xd63f0100},5},
};
static const uint32_t dm_native_hd_target_entry[3][8]={
    {0xa9ba6ffc,0xa90167fa,0xa9025ff8,0xa90357f6,0xa9044ff4,0xa9057bfd,0x910143fd,0xd10903ff},
    {0xa9ba6ffc,0xa90167fa,0xa9025ff8,0xa90357f6,0xa9044ff4,0xa9057bfd,0x910143fd,0xd10943ff},
    {0xa9bb67fa,0xa9015ff8,0xa90257f6,0xa9034ff4,0xa9047bfd,0x910103fd,0xf000fc88,0x91214108},
};
static const uint32_t dm_native_hd_toggle[]={
    0xd103c3ff,0xa90b5ff8,0xa90c57f6,0xa90d4ff4,0xa90e7bfd,0x910383fd,0x90011f88,0x91112108,0x08dffd08,0x37002328,0x90011f94,0xf9422e80,0xb4000200,0x90009041,0x911b2021,0x94001b79,0xaa0003f3,0xf9422e80,0xb4000080,0xb0009181,0x91235c21,0x94001b73,0xb4000073,0x39414668,0x35002148,0xb4000060,0x39414408,0x350020e8,0xf0011048,0xf945c108,0xb40000a8,0x39417108,0x51000508,0x7100051f,0xd503201f,0xf0011548,0x911d7108,0x2d400500,0x1e212000,0x54001f61,0xf0011548,0x911d6108,0xbd400100,0x1e202008,0x1e2e1000,0x1e2703e1
};
