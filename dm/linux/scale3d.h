// INPUT: Source-anchored 90447/94012 renderer and opt-in Linux 3D scale settings.
// OUTPUT: Original-engine target rebuild; output dimensions and UI stay fixed.
// POS: Called on the original notification thread, never inside GPU present.
#ifndef DM_LINUX_SCALE3D_H
#define DM_LINUX_SCALE3D_H
#ifdef DM_LINUX_GFX
#include <math.h>
#include <sys/uio.h>

extern uint64_t dm_nvn_frame_count(void);
extern void dm_linux_renderer_rebuild_prepare(void);
extern int dm_linux_renderer_rebuild_ready(void);
extern void dm_linux_renderer_rebuild_finish(void);
static const struct { uint32_t address,count,words[29]; } linux_s3_anchors[]={
    {0xca41a0,10,{0xd10283ff,0xa9046ffc,0xa90567fa,0xa9065ff8,0xa90757f6,0xa9084ff4,0xa9097bfd,0x910243fd,0x910033e0,0x9434fec7}},
    {0xca4300,9,{0x17ffffc6,0xa9497bfd,0xa9484ff4,0xa94757f6,0xa9465ff8,0xa94567fa,0xa9446ffc,0x910283ff,0xd65f03c0}},
    {0x728a80,9,{0xd10343ff,0xa9095ff8,0xa90a57f6,0xa90b4ff4,0xa90c7bfd,0x910303fd,0xb00147a8,0xf9430108,0xb4000d28}},
    {0x728bcc,4,{0xd00147b7,0xf9455ae9,0x528cbfaa,0x382a6928}},
    {0x728c34,4,{0xf9455ae0,0x2a1403e1,0x2a1303e2,0x943618a2}},
    {0x14aeec8,29,{0xf81b0ff9,0xa9015ff8,0xa90257f6,0xa9034ff4,0xa9047bfd,0x910103fd,0x1e230021,0x528cd488,0xbc686800,0x1e210801,0x529dcf0a,0x1e250028,0x8b0a0017,0x1e230041,0x1e210800,0x1e250009,0xb94002ea,0xaa0003f3,0x6b08015f,0x54000081,0xb94006ea,0x6b09015f,0x540000e0,0x7100011f,0x7a401924,0x29020ae1,0x290026e8,0x290126e8,0x540000e1}},
};
#ifdef DM_LINUX_GAME_94012
#include "../android/d2r_scale3d_profile.h"
#define LINUX_S3_ANCHORS dm_s3_anchors
static const struct {const char *id,*build;uint32_t text,ro,data,size,global,rebuild,call0,call1;} linux_s3_profile={
    "db0507f36b84200ac05f75d76ed5026f","94012",0x1b786a0,0x1b79000,0x2056000,0x3214000,0x31e0848,0x8582f0,0xe10d58,0xe10db0};
#else
#define LINUX_S3_ANCHORS linux_s3_anchors
static const struct {const char *id,*build;uint32_t text,ro,data,size,global,rebuild,call0,call1;} linux_s3_profile={
    "c5e0c2db39a8d8a3ae2d5dfaaa68e16e","90447",0x19e4b20,0x19e5000,0x1ea4000,0x306bab8,0x301eab0,0x728a80,0xca41c8,0xca4220};
#endif
static int linux_s3_read(uintptr_t address,void *out,size_t size) {
    if(!address || address>UINTPTR_MAX-size)return 0;
    struct iovec local={out,size},remote={(void *)address,size};
    return process_vm_readv(getpid(),&local,1,&remote,1,0)==(ssize_t)size;
}
static void linux_scale3d_tick(void *return_address) {
    static int configured,enabled,checked,have_owner,warming;
    static float want=1.f;
    static const char *control;
    static uint64_t next_poll,sequence;
    static pthread_t owner;
    static _Thread_local int rebuilding;
    if(rebuilding)return;
    if(!configured) {
        const char *value=getenv("DM_LINUX_3D_SCALE");
        control=getenv("DM_LINUX_3D_SCALE_CONTROL");
        enabled=value || control;
        if(value) {
            char *end;errno=0;want=strtof(value,&end);
            if(end==value || *end || errno || !isfinite(want) || want<.5f || want>1.f)
                die("Linux 3D scale requires a finite value 0.5..1.0");
        }
        if(control && control[0]!='/')die("Linux 3D scale control requires an absolute path");
        configured=1;
    }
    if(!enabled || dm_nvn_frame_count()<30)return;
#ifdef DM_LINUX_GAME_94012
    /* The existing 94012 default was a no-op at 1. Keep classic-only builds
     * out of optional 3D profile guards unless a change/control was requested. */
    if(want==1.f&&!control)return;
#endif
    module_t *m=NULL;
    for(int i=0;i<g_nmod;i++)if(!strcmp(g_mod[i].name,"main")){m=&g_mod[i];break;}
    if(!m || strcmp(m->build_id,linux_s3_profile.id) || m->text_end!=linux_s3_profile.text ||
       m->ro_off!=linux_s3_profile.ro || m->data_off!=linux_s3_profile.data || m->size<linux_s3_profile.size)
        die("Linux 3D scale requires the reviewed original module layout");
    uintptr_t caller=(uintptr_t)return_address-(uintptr_t)m->base;
    if(caller!=linux_s3_profile.call0 && caller!=linux_s3_profile.call1)return;
    if(have_owner && !pthread_equal(owner,pthread_self()))die("Linux 3D scale notification thread changed");
    if(!checked) {
        uint32_t words[32];
        for(size_t i=0;i<sizeof LINUX_S3_ANCHORS/sizeof *LINUX_S3_ANCHORS;i++) {
            size_t size=LINUX_S3_ANCHORS[i].count*4;
            if(!linux_s3_read((uintptr_t)m->base+LINUX_S3_ANCHORS[i].address,words,size) ||
               memcmp(words,LINUX_S3_ANCHORS[i].words,size))die("Linux 3D scale original instruction guard failed");
        }
        owner=pthread_self();have_owner=1;checked=1;
        LOG("LINUX_3D_SCALE_PROFILE build=%s guards=%zu UI_output_unchanged=1 classic_world_unchanged=1",linux_s3_profile.build,sizeof LINUX_S3_ANCHORS/sizeof *LINUX_S3_ANCHORS);
    }
    uint64_t now=now_ns();
    if(control && now>=next_poll) {
        next_poll=now+200000000;
        int fd=open(control,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
        if(fd>=0) {
            struct stat st;
            if(!fstat(fd,&st) && S_ISREG(st.st_mode) && st.st_uid==getuid() && st.st_size>0 && st.st_size<128) {
                char line[128],extra;unsigned long long pid,seq;float scale;
                ssize_t n=read(fd,line,sizeof line-1);
                if(n>0) {
                    line[n]=0;
                    if(sscanf(line,"%llu %llu %f %c",&pid,&seq,&scale,&extra)==3 &&
                       pid==(unsigned long long)getpid() && seq>sequence && isfinite(scale) && scale>=.5f && scale<=1.f) {
                        sequence=seq;want=scale;
                        LOG("LINUX_3D_SCALE_REQUEST sequence=%llu scale=%.3f",seq,(double)want);
                    }
                }
            }
            close(fd);
        }
    }
    uintptr_t renderer=0;uint32_t sizes[6];float before;
    if(!linux_s3_read((uintptr_t)m->base+linux_s3_profile.global,&renderer,8) || !renderer || (renderer&15) ||
       renderer>UINTPTR_MAX-0xee90 || !linux_s3_read(renderer+0xee78,sizes,sizeof sizes) ||
       !linux_s3_read(renderer+0x66a4,&before,4) || !isfinite(before) || before<.5f || before>1.f ||
       !sizes[4] || !sizes[5] || sizes[4]>4096 || sizes[5]>2160) {
        if(warming){dm_linux_renderer_rebuild_finish();warming=0;}
        return;
    }
    if(want==before) {
        if(warming){dm_linux_renderer_rebuild_finish();warming=0;}
        return;
    }
    if(!warming) {
        dm_linux_renderer_rebuild_prepare();warming=1;
        LOG("LINUX_3D_SCALE_WARMUP sequence=%llu full_frames_before=3 full_frames_after=10",
            (unsigned long long)sequence);
        return;
    }
    if(!dm_linux_renderer_rebuild_ready())return;
    uint32_t width=(uint32_t)lroundf(sizes[4]*want),height=(uint32_t)lroundf(sizes[5]*want);
    LOG("LINUX_3D_SCALE_BEGIN scale=%.3f internal=%ux%u output=%ux%u frame=%llu",
        (double)want,sizes[0],sizes[1],sizes[4],sizes[5],(unsigned long long)dm_nvn_frame_count());
    rebuilding=1;
    dm_guest_write_prepare((void *)(renderer+0x66a4),4);
    struct iovec local={&want,4},remote={(void *)(renderer+0x66a4),4};
    if(process_vm_writev(getpid(),&local,1,&remote,1,0)!=4)die("Linux 3D scale write failed");
    ((void (*)(uint32_t,uint32_t))(m->base+linux_s3_profile.rebuild))(sizes[4],sizes[5]);
    uintptr_t current=0;uint32_t after[6];float actual=0;
    if(!linux_s3_read((uintptr_t)m->base+linux_s3_profile.global,&current,8) || current!=renderer ||
       !linux_s3_read(renderer+0xee78,after,sizeof after) || !linux_s3_read(renderer+0x66a4,&actual,4) || actual!=want ||
       after[0]!=width || after[1]!=height || after[2]!=width || after[3]!=height || after[4]!=sizes[4] || after[5]!=sizes[5])
        die("Linux 3D scale rebuild postcondition failed");
    dm_linux_renderer_rebuild_finish();warming=0;rebuilding=0;
    LOG("LINUX_3D_SCALE_END sequence=%llu scale=%.3f internal=%ux%u output=%ux%u UI_output_unchanged=1 rebuild_ms=%.3f frame=%llu",
        (unsigned long long)sequence,(double)want,after[0],after[1],after[4],after[5],(now_ns()-now)/1e6,
        (unsigned long long)dm_nvn_frame_count());
}
#undef LINUX_S3_ANCHORS
#else
static void linux_scale3d_tick(void *return_address) { (void)return_address; }
#endif
#endif
