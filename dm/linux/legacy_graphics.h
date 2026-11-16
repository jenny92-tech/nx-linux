// INPUT: Opt-in classic-graphics experiment and the verified 90447 main module.
// OUTPUT: Original toggle request, with availability/state logged explicitly.
// POS: Diagnostic-only original UI-thread call; never changes saves or input devices.
#ifndef DM_LINUX_LEGACY_GRAPHICS_H
#define DM_LINUX_LEGACY_GRAPHICS_H
#ifdef DM_LINUX_GFX
static void linux_legacy_graphics_tick(void *return_address) {
    static int configured,enabled,checked,reported;
    static uint64_t next;
    static const uint32_t toggle[]={
        0xa9be4ff4,0xa9017bfd,0x910043fd,0xd0015008,0x913b6108,0x08dffd08,0x37000448,0xd0015014,0xf9477680,0xb4000200,
        0xb000c261,0x9115f821,0x941959ac,0xaa0003f3,0xf9477680,0xb4000080,0x9000c3a1,0x9128cc21,0x941959a6,0xb4000073,
        0x39414668,0x35000268,0xb4000060,0x39414408,0x35000208,0x90014668,0x9132b108,0x2d400500,0x1e212000,0x54000161,
        0x90014668,0x9132a108,0xbd400100,0x1e202008,0x1e2e1000,0x1e2703e1,0x1e20dc22,0x1e21dc00,0x2d008102,0xb9000d1f,
        0xa9417bfd,0xa8c24ff4,0xd65f03c0
    };
    if(!configured) {
        const char *value=getenv("DM_LINUX_LEGACY_GRAPHICS");
        if(value && strcmp(value,"0") && strcmp(value,"1"))die("Linux legacy experiment requires 0 or 1");
        enabled=value && !strcmp(value,"1");configured=1;
    }
    if(!enabled || dm_nvn_frame_count()<30 || now_ns()<next)return;
    module_t *m=NULL;
    for(int i=0;i<g_nmod;i++)if(!strcmp(g_mod[i].name,"main")){m=&g_mod[i];break;}
    if(!m || strcmp(m->build_id,"c5e0c2db39a8d8a3ae2d5dfaaa68e16e") ||
       m->text_end!=0x19e4b20 || m->ro_off!=0x19e5000 || m->data_off!=0x1ea4000)
        die("Linux legacy experiment requires reviewed 90447 main");
    uintptr_t caller=(uintptr_t)return_address-(uintptr_t)m->base;
    if(caller!=0xca41c8 && caller!=0xca4220)return;
    next=now_ns()+1000000000;
    if(!checked) {
        uint32_t words[43];
        if(!linux_s3_read((uintptr_t)m->base+0x33e750,words,sizeof words) || memcmp(words,toggle,sizeof words))
            die("Linux legacy original function guard failed");
        checked=1;
    }
    // Only +0/+4/+8 are interpolated coefficients. The +12 transition
    // counter is opaque here; the original function alone owns its units.
    float state[3];
    if(!linux_s3_read((uintptr_t)m->base+0x2c0aca8,state,sizeof state))die("Linux legacy transition state unavailable");
    for(unsigned i=0;i<3;i++)if(!isfinite(state[i]) || state[i]<0.f || state[i]>1.f)
        die("Linux legacy coefficients outside original bounds: %a %a %a",(double)state[0],(double)state[1],(double)state[2]);
    if(state[0]==0.f && state[2]==0.f) {
        if(!reported)LOG("LINUX_LEGACY_ACTIVE frame=%llu original_toggle=1 visual_verified=0",
            (unsigned long long)dm_nvn_frame_count());
        reported=1;return;
    }
    if(state[2]==0.f || state[1]!=state[2])return;
    ((void (*)(void))(m->base+0x33e750))();
    float after[3];
    if(!linux_s3_read((uintptr_t)m->base+0x2c0aca8,after,sizeof after))die("Linux legacy transition read failed");
    LOG("LINUX_LEGACY_REQUEST target_before=%.3f target_after=%.3f accepted=%d frame=%llu",
        (double)state[2],(double)after[2],after[2]==0.f,(unsigned long long)dm_nvn_frame_count());
}
#else
static void linux_legacy_graphics_tick(void *return_address) { (void)return_address; }
#endif
#endif
