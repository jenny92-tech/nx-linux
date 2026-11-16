// INPUT:  Explicit Linux launch settings and the shared HLE registration table.
// OUTPUT:  Platform-service values used during original static initialization.
// POS:    Replace Horizon's language query without using another game's offsets.
#include "../host/hle.h"
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "scale3d.h"
#include "legacy_graphics.h"
#include "native_hd.h"
#include "auto_classic_panel.h"
#include "runtime_build.h"
#ifndef DM_LINUX_SOULKNIGHT
__attribute__((used)) static const char linux_runtime_game_build[]="D2R-LINUX-GAME-BUILD=" DM_NATIVE_HD_GAME_BUILD;
#endif
#ifdef DM_LINUX_GFX
static int linux_panel_read(void *user,uintptr_t address,void *out,size_t size){
    (void)user;return linux_s3_read(address,out,size);
}
static pthread_mutex_t linux_panel_context_lock=PTHREAD_MUTEX_INITIALIZER;
static int linux_auto_classic_context_locked(const module_t *m,int allow_settings){
    static uintptr_t checked,bad;static int previous[2]={-2,-2};
    uintptr_t base=(uintptr_t)m->base,manager,again;
    if(checked!=base){
        /* Original GetPanel list walks establish pointer/count and name
         * offsets; the original toggle establishes visibility +81. */
        static const uint32_t first[]={0xa9be4ff4,0xa9017bfd,0x910043fd,0xf9404808,
            0xb4000208,0xf9404409,0x8b080d2a,0xf9400128,0xaa0103ec,0xf940050b,
            0x3840156d,0x3840158e,0x3400006d,0x6b0e01bf,0x54ffff80,0x6b0e01bf,
            0x54000740,0x91002129,0xeb0a013f,0x54fffe81};
        static const uint32_t third[]={0xf9403009,0xb4000469,0xf9402c08,0x8b090d09,
            0xf9400113,0xaa0103eb,0xf940066a,0x3840154c,0x3840156d,0x3400006c,
            0x6b0d019f,0x54ffff80,0x6b0d019f,0x540000a0,0x91002108,0xeb09011f,
            0x54fffe81,0x14000013,0xb4000253};
        uint32_t words[sizeof dm_native_hd_toggle/4];
        if(!linux_s3_read(base+DM_NATIVE_HD_PANEL_FIRST,words,sizeof first)||memcmp(words,first,sizeof first)||
           !linux_s3_read(base+DM_NATIVE_HD_PANEL_THIRD,words,sizeof third)||memcmp(words,third,sizeof third)||
           !linux_s3_read(base+DM_NATIVE_HD_TOGGLE,words,sizeof dm_native_hd_toggle)||
           memcmp(words,dm_native_hd_toggle,sizeof dm_native_hd_toggle)){
            if(bad!=base)LOG("LINUX_AUTO_CLASSIC_CONTEXT guards_verified=0 input_suppressed=1");
            bad=base;return -1;
        }
        checked=base;
        LOG("LINUX_AUTO_CLASSIC_CONTEXT guards_verified=1 build=%s read_only=1",DM_NATIVE_HD_GAME_BUILD);
    }
    if(!linux_s3_read(base+DM_NATIVE_HD_PANEL_MANAGER,&manager,sizeof manager))return -1;
    int ready=dm_auto_classic_panel_context(linux_panel_read,NULL,manager,base+DM_NATIVE_HD_HUD_VTABLE,allow_settings);
    if(!linux_s3_read(base+DM_NATIVE_HD_PANEL_MANAGER,&again,sizeof again)||manager!=again)return -1;
    if(ready!=previous[allow_settings]){LOG("LINUX_AUTO_CLASSIC_CONTEXT hud_ready=%d main_menu_ready=%d menu_input_suppressed=%d scene_policy=%d context=%d",ready==1,ready==2,ready!=1,allow_settings,ready);previous[allow_settings]=ready;}
    return ready;
}
static int linux_auto_classic_context(const module_t *m){
    pthread_mutex_lock(&linux_panel_context_lock);int result=linux_auto_classic_context_locked(m,0);pthread_mutex_unlock(&linux_panel_context_lock);
    return result;
}
/* Read only the reviewed title's original target-mode word. Menus/loading
 * retain their original input routing; no guest toggle function is called. */
int dm_linux_classic_target_mode(void){
    if(dm_nvn_frame_count()<30)return -1;
    for(int i=0;i<g_nmod;i++)if(!strcmp(g_mod[i].name,"main")){
        const module_t *m=&g_mod[i];uint32_t target;
        if(strcmp(m->build_id,DM_NATIVE_HD_BUILD)||m->text_end!=DM_NATIVE_HD_TEXT_END||
           m->data_off!=DM_NATIVE_HD_DATA||m->size<DM_NATIVE_HD_BLEND+12||
           !linux_s3_read((uintptr_t)m->base+DM_NATIVE_HD_BLEND+8,&target,4))return -1;
        if(target!=0&&target!=0x3f800000)return -1;
        int context=linux_auto_classic_context(m);
        if(context==2)return 2;
        if(target==0)return 0;
        return context==1?1:-1;
    }
    return -1;
}
/* Residency changes need both the classic target and a verified live HUD.
 * target mode zero alone also occurs while loading or behind modal menus. */
int dm_linux_classic_gameplay_context(void){
    if(dm_nvn_frame_count()<30)return 0;
    for(int i=0;i<g_nmod;i++)if(!strcmp(g_mod[i].name,"main")){
        const module_t *m=&g_mod[i];uint32_t target;
        if(strcmp(m->build_id,DM_NATIVE_HD_BUILD)||m->text_end!=DM_NATIVE_HD_TEXT_END||
           m->data_off!=DM_NATIVE_HD_DATA||m->size<DM_NATIVE_HD_BLEND+12||
           !linux_s3_read((uintptr_t)m->base+DM_NATIVE_HD_BLEND+8,&target,4)||target!=0)return 0;
        return linux_auto_classic_context(m)==1;
    }
    return 0;
}
int dm_linux_menu_scene_context(void){
    if(dm_nvn_frame_count()<30)return 0;
    for(int i=0;i<g_nmod;i++)if(!strcmp(g_mod[i].name,"main")){
        const module_t *m=&g_mod[i];
        if(strcmp(m->build_id,DM_NATIVE_HD_BUILD)||m->text_end!=DM_NATIVE_HD_TEXT_END||
           m->data_off!=DM_NATIVE_HD_DATA||m->size<DM_NATIVE_HD_BLEND+12)return 0;
        pthread_mutex_lock(&linux_panel_context_lock);int result=linux_auto_classic_context_locked(m,1);pthread_mutex_unlock(&linux_panel_context_lock);
        return result>=2&&result<=5?result:0;
    }
    return 0;
}
int dm_linux_exit_menu_ready(void){
    if(dm_nvn_frame_count()<30)return 0;
    for(int i=0;i<g_nmod;i++)if(!strcmp(g_mod[i].name,"main")){
        const module_t *m=&g_mod[i];uintptr_t manager,again;int ready=0;
        if(strcmp(m->build_id,DM_NATIVE_HD_BUILD)||m->text_end!=DM_NATIVE_HD_TEXT_END||m->data_off!=DM_NATIVE_HD_DATA)return 0;
        pthread_mutex_lock(&linux_panel_context_lock);
        if(linux_auto_classic_context_locked(m,0)==2&&
           linux_s3_read((uintptr_t)m->base+DM_NATIVE_HD_PANEL_MANAGER,&manager,sizeof manager)){
            ready=dm_auto_classic_exit_main(linux_panel_read,NULL,manager);
            if(!linux_s3_read((uintptr_t)m->base+DM_NATIVE_HD_PANEL_MANAGER,&again,sizeof again)||manager!=again)ready=0;
        }
        pthread_mutex_unlock(&linux_panel_context_lock);return ready;
    }
    return 0;
}
/* Reproduce the original 90447 value-read callbacks, rather than mistaking
 * a Cvar's registration default/range fields for its current graphics value. */
static int linux_settings_value_callback(const module_t *m,uintptr_t storage,uint32_t *value) {
    uintptr_t tagged,table,function;
    if(!linux_s3_read(storage,&tagged,sizeof tagged))return 0;
    uintptr_t object=(tagged&1)?tagged&~(uintptr_t)1:storage;
    if(!linux_s3_read(object,&table,sizeof table)||
       !linux_s3_read(table+0x10,&function,sizeof function))return 0;
    /* Only emulate reviewed, read-only 90447 callbacks. Do not call
     * an arbitrary function merely because its pointer is in executable text. */
    static const uint32_t clamp[]={0xaa0003e8,0xb9400029,0xb9400800,0x6b09001f,
        0x5400008c,0xb9400d08,0x6b09011f,0x1a89b100,0xd65f03c0};
    static const uint32_t identity[]={0xb9400020,0xd65f03c0};
    uint32_t actual[9];uintptr_t base=(uintptr_t)m->base;
    if(function==base+0x2eda10&&linux_s3_read(function,actual,sizeof identity)&&!memcmp(actual,identity,sizeof identity))return 1;
    if(function==base+0x102d788&&linux_s3_read(function,actual,sizeof clamp)&&!memcmp(actual,clamp,sizeof clamp)){
        int32_t bounds[2],current;memcpy(&current,value,4);
        if(!linux_s3_read(object+8,bounds,sizeof bounds))return 0;
        if(current<bounds[0])*value=(uint32_t)bounds[0];
        else if(current>bounds[1])*value=(uint32_t)bounds[1];
        return 1;
    }
    /* These getters convert the internal range to the settings-file enum.
     * Texture/atmosphere/detail zero are stored as 2, shadow as 1,
     * transparency as 3.
     * Raw Cvar values must not be reported as effective quality levels. */
    static const struct { uintptr_t offset;uint32_t subtract,instruction; } enums[]={
        {0x2fde70,2,0x51000900},{0x2fe80c,1,0x51000500},
        {0x2fe4a4,2,0x51000900},{0x2fe658,3,0x51000d00},
        {0x2fe13c,2,0x51000900},{0x2fe2f0,2,0x51000900}
    };
    for(unsigned i=0;i<sizeof enums/sizeof *enums;i++) {
        uint32_t expected[]={0xb9400028,enums[i].instruction,0xd65f03c0};
        if(function==base+enums[i].offset&&linux_s3_read(function,actual,sizeof expected)&&
           !memcmp(actual,expected,sizeof expected)){
            *value-=enums[i].subtract;return 1;
        }
    }
    return 0;
}
static void linux_settings_audit(void) {
    static int configured,enabled;static uint64_t next;
    if(!configured){enabled=getenv("DM_LINUX_SETTINGS_AUDIT")!=NULL;configured=1;}
    if(!enabled || dm_nvn_frame_count()<30 || now_ns()<next)return;
    next=now_ns()+5000000000;
    module_t *m=NULL;for(int i=0;i<g_nmod;i++)if(!strcmp(g_mod[i].name,"main")){m=&g_mod[i];break;}
    if(!m || strcmp(m->build_id,"c5e0c2db39a8d8a3ae2d5dfaaa68e16e")||m->text_end!=0x19e4b20)return;
    static const struct { const char *name;uintptr_t offset; } settings[]={
        {"Texture Quality",0x2b9aa48},{"Shadow Quality",0x2b9b490},
        {"Light Quality",0x2b99cf8},{"Atmospherics Quality",0x2b9b1a0},
        {"Transparency Quality",0x2b9b318},{"Vfx Quality",0x2b9b8f8},
        {"Ambient Occlusion Quality",0x2b9ad38},{"Anti Aliasing",0x2b9b608},{"Vfx Lighting Quality",0x2b9ba70},
        {"Character Detail",0x2b9aeb0},{"Environment Detail",0x2b9b028},{"Texture Anisotropy",0x2b9abc0}
    };
    for(unsigned i=0;i<sizeof settings/sizeof *settings;i++) {
        uint32_t words[9];
        if(linux_s3_read((uintptr_t)m->base+settings[i].offset+0x20,words,sizeof words))
            LOG("LINUX_SETTINGS_AUDIT name=%s fields20_40=%#x,%#x,%#x,%#x,%#x,%#x,%#x,%#x,%#x frame=%llu",
                settings[i].name,words[0],words[1],words[2],words[3],words[4],words[5],words[6],words[7],words[8],
                (unsigned long long)dm_nvn_frame_count());
        if(getenv("DM_LINUX_SETTINGS_VALUES")) {
            uintptr_t object=(uintptr_t)m->base+settings[i].offset;
            uint32_t value;
            if(linux_s3_read(object+0xc,&value,sizeof value)&&
               linux_settings_value_callback(m,object+0xc8,&value)&&
               linux_settings_value_callback(m,object+0x118,&value))
                LOG("LINUX_SETTINGS_VALUE name=%s value=%u frame=%llu",settings[i].name,value,
                    (unsigned long long)dm_nvn_frame_count());
            else LOG("LINUX_SETTINGS_VALUE_UNVERIFIED name=%s frame=%llu",settings[i].name,
                (unsigned long long)dm_nvn_frame_count());
        }
    }
}
#else
static void linux_settings_audit(void) {}
#endif

static pthread_once_t mode_once=PTHREAD_ONCE_INIT, identity_once=PTHREAD_ONCE_INIT;
static _Atomic int operation_mode, focus_state=1;
static pthread_mutex_t notification_lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned operation_notifications, performance_notifications;
static uint32_t notifications[4];
#ifdef DM_LINUX_SOULKNIGHT
static unsigned soul_exit_depth;static int soul_exit_pending;static uint64_t soul_exit_deadline;
#endif
static unsigned notification_count;
static uint8_t device_identity[16];

static void mode_init(void) {
    const char *value=getenv("DM_OPMODE");
    if(value && strcmp(value,"0") && strcmp(value,"1"))die("Linux DM_OPMODE must be 0 or 1");
    atomic_store(&operation_mode,value ? atoi(value) : 0);
}
int dm_opmode(void) { pthread_once(&mode_once,mode_init);return atomic_load(&operation_mode); }
static void notify_locked(uint32_t message) {
    for(unsigned i=0;i<notification_count;i++)if(notifications[i]==message)return;
    if(notification_count==4)die("Linux platform notification capacity");
    notifications[notification_count++]=message;
}
#ifdef DM_LINUX_SOULKNIGHT
/* ISelfController exit locking gives the guest a cleanup interval after
 * notification 4. Match libnx appletLockExit's documented 15-second bound;
 * never let a normal SDL close interrupt a protected save immediately. */
HLE_FORCE("_ZN2nn2oe31EnterExitRequestHandlingSectionEv", "Defer normal host exit while the guest handles its exit request", void,soul_oe_enter_exit,void) {
    pthread_mutex_lock(&notification_lock);
    if(soul_exit_depth==UINT32_MAX)die("Linux exit handling nesting overflow");
    ++soul_exit_depth;pthread_mutex_unlock(&notification_lock);
}
HLE_FORCE("_ZN2nn2oe31LeaveExitRequestHandlingSectionEv", "Release a guest exit handling section after cleanup", void,soul_oe_leave_exit,void) {
    pthread_mutex_lock(&notification_lock);if(!soul_exit_depth)die("Linux unmatched exit handling leave");
    --soul_exit_depth;pthread_mutex_unlock(&notification_lock);
}
int soul_oe_request_exit(void) {
    pthread_mutex_lock(&notification_lock);
    if(!soul_exit_pending){soul_exit_pending=1;soul_exit_deadline=now_ns()+15000000000ull;notify_locked(4);}
    int ready=!soul_exit_depth || now_ns()>=soul_exit_deadline;
    pthread_mutex_unlock(&notification_lock);return ready;
}
int soul_oe_exit_ready(void) {
    pthread_mutex_lock(&notification_lock);
    int ready=soul_exit_pending && (!soul_exit_depth || now_ns()>=soul_exit_deadline);
    pthread_mutex_unlock(&notification_lock);return ready;
}
HLE_FORCE("_ZN2nn2oe15ExitApplicationEv", "Guest completed cleanup and explicitly exits its host process", void,soul_oe_exit_application,void) {
    pthread_mutex_lock(&notification_lock);soul_exit_depth=0;pthread_mutex_unlock(&notification_lock);
    void dm_host_shutdown(const char *);dm_host_shutdown("nn::oe::ExitApplication");
}
#endif
void dm_focus_set(int state) {
    if(state<1||state>3)die("Linux invalid focus state %d",state);
    if(atomic_exchange(&focus_state,state)==state)return;
    LOG("LINUX_FOCUS_STATE state=%d",state);
#ifdef DM_LINUX_GFX
    dm_audio_suspend(state!=1);
#endif
    pthread_mutex_lock(&notification_lock);notify_locked(15);pthread_mutex_unlock(&notification_lock);
}
void dm_set_opmode(int mode) {
    if(mode!=0&&mode!=1)die("Linux invalid operation mode %d",mode);
    (void)dm_opmode();
    if(atomic_exchange(&operation_mode,mode)==mode)return;
    pthread_mutex_lock(&notification_lock);
    if(operation_notifications)notify_locked(30);
    if(performance_notifications)notify_locked(31);
    pthread_mutex_unlock(&notification_lock);
}
static void identity_io(int fd,void *bytes,size_t size,int writing) {
    size_t done=0;
    while(done<size) {
        ssize_t n=writing ? write(fd,(uint8_t *)bytes+done,size-done) : read(fd,(uint8_t *)bytes+done,size-done);
        if(n<0&&errno==EINTR)continue;
        if(n<=0)die("Linux private device identity %s failed errno=%d",writing?"write":"read",errno);
        done+=(size_t)n;
    }
}
static void identity_init(void) {
    const char *path=getenv("DM_PROFILE_DIR");
    if(!path||path[0]!='/')die("Set DM_PROFILE_DIR to an owned absolute profile directory");
    int directory=open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if(directory<0)die("Linux private profile directory open failed errno=%d",errno);
    int fd=openat(directory,"device-id.bin",O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0&&errno==ENOENT) {
        arc4random_buf(device_identity,sizeof device_identity);
        device_identity[6]=(device_identity[6]&15u)|64u;
        device_identity[8]=(device_identity[8]&63u)|128u;
        char temporary[64];
        snprintf(temporary,sizeof temporary,"device-id.%d.%08x.new",getpid(),arc4random());
        int output=openat(directory,temporary,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
        if(output<0)die("Linux private identity create failed errno=%d",errno);
        identity_io(output,device_identity,sizeof device_identity,1);
        int flushed=fsync(output), flush_error=errno;
        int closed=close(output);
        if(flushed||closed)die("Linux private identity flush failed errno=%d",flushed?flush_error:errno);
        if(syscall(SYS_renameat2,directory,temporary,directory,"device-id.bin",1u)) {
            int error=errno;
            unlinkat(directory,temporary,0);
            if(error!=EEXIST)die("Linux private identity publish failed errno=%d",error);
        }
        if(fsync(directory))die("Linux private profile sync failed errno=%d",errno);
        fd=openat(directory,"device-id.bin",O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    }
    if(fd<0)die("Linux private identity open failed errno=%d",errno);
    struct stat status;
    if(fstat(fd,&status)||!S_ISREG(status.st_mode)||status.st_size!=sizeof device_identity)
        die("Linux private identity must be a 16-byte regular file");
    identity_io(fd,device_identity,sizeof device_identity,0);
    close(fd);close(directory);
    if((device_identity[6]&240u)!=64u||(device_identity[8]&192u)!=128u)
        die("Linux private identity is not a valid local UUID");
}

HLE_FORCE("_ZN2nn2oe18GetDesiredLanguageEv", "Linux language configuration replaces unavailable Horizon oe service", uint64_t, linux_language, void) {
    const char *language = getenv("DM_LANG");
    char code[8] = {0};
    strlcpy(code, language ? language : "zh-Hans", sizeof code);
    uint64_t result;
    memcpy(&result, code, sizeof result);
    return result;
}

HLE_FORCE("_ZN2nn2oe27SetPerformanceConfigurationENS0_15PerformanceModeEi",
    "Guest Tegra frequency hints are diagnostic only; native Linux governors retain clock ownership", void,
    linux_performance_configuration, int mode, unsigned configuration) {
    if(mode!=0&&mode!=1)die("Linux invalid performance hint mode %d",mode);
    LOG("LINUX_APM unsupported Tegra request mode=%d configuration=%#x; native Linux governors retained", mode, configuration);
}

HLE_FORCE("_ZN2nn2oe16GetOperationModeEv", "Selected Linux guest execution profile", int, linux_operation_mode, void) {return dm_opmode();}
HLE_FORCE("_ZN2nn2oe18GetPerformanceModeEv", "Guest profile query, independent of physical Linux clock rates", int, linux_performance_mode, void) {return dm_opmode();}
HLE_FORCE("_ZN2nn2oe20GetCurrentFocusStateEv", "Actual SDL foreground state", int, linux_focus, void) {return atomic_load(&focus_state);}
HLE_FORCE("_ZN2nn2oe25TryPopNotificationMessageEPj", "SDL/configuration state invalidation notifications", unsigned, linux_notification, uint32_t *out) {
    linux_scale3d_tick(__builtin_return_address(0));
    linux_legacy_graphics_tick(__builtin_return_address(0));
    linux_settings_audit();
    if(!out)die("Linux notification output is null");
    pthread_mutex_lock(&notification_lock);
    unsigned present=notification_count!=0;
    if(present){*out=notifications[0];--notification_count;memmove(notifications,notifications+1,notification_count*sizeof *notifications);}
    pthread_mutex_unlock(&notification_lock);return present;
}
HLE_FORCE("_ZN2nn2oe42SetOperationModeChangedNotificationEnabledEb", "Enable notifications for real guest-profile changes", void, linux_operation_notifications, unsigned enabled) {
    pthread_mutex_lock(&notification_lock);operation_notifications=(enabled&255u)!=0;pthread_mutex_unlock(&notification_lock);
}
HLE_FORCE("_ZN2nn2oe44SetPerformanceModeChangedNotificationEnabledEb", "Enable notifications for real guest-profile changes", void, linux_performance_notifications, unsigned enabled) {
    pthread_mutex_lock(&notification_lock);performance_notifications=(enabled&255u)!=0;pthread_mutex_unlock(&notification_lock);
}
HLE_FORCE("_ZN2nn2oe28SetResumeNotificationEnabledEb", "SDL focus notifications carry the actual resume/foreground state", void, linux_resume_notifications, unsigned enabled) {
    LOG("LINUX_OE resume notifications=%u; SDL foreground changes use focus invalidations",(enabled&255u)!=0);
}
HLE_FORCE("_ZN2nn2oe20SetFocusHandlingModeENS0_17FocusHandlingModeE", "SDL reports focus without suspending guest workers", void, linux_focus_handling, int mode) {
    if(mode!=1)die("Linux focus handling mode %d requires a suspend/resume implementation",mode);
}
HLE_FORCE("_ZN2nn2oe17GetPseudoDeviceIdEPNS_4util4UuidE", "Persistent anonymous identity scoped to the selected local profile", void, linux_device_id, uint8_t *out) {
    if(!out)die("Linux device identity output is null");
    pthread_once(&identity_once,identity_init);memcpy(out,device_identity,sizeof device_identity);
}
HLE_FORCE("_ZN2nn2oe15EnableRecordingEv", "No Horizon recording UI; policy request logged", void, linux_enable_recording, void) {LOG("LINUX_OE recording policy enabled; no Horizon recorder");}
HLE_FORCE("_ZN2nn2oe16DisableRecordingEv", "No Horizon recording UI; policy request logged", void, linux_disable_recording, void) {LOG("LINUX_OE recording policy disabled");}
HLE_FORCE("_ZN2nn2oe16EnableScreenShotEv", "No Horizon screenshot UI; explicit host diagnostics remain separate", void, linux_enable_screenshot, void) {LOG("LINUX_OE screenshot policy enabled; no Horizon gallery");}
HLE_FORCE("_ZN2nn2oe17DisableScreenShotEv", "No Horizon screenshot UI; explicit host diagnostics remain separate", void, linux_disable_screenshot, void) {LOG("LINUX_OE screenshot policy disabled");}

HLE_FORCE("_ZN2nn3nsd9ResolveExEPNS0_4FqdnERKS1_", "Offline host does not resolve Nintendo service domains", Result,
    linux_service_domain, void *out, const void *input) {
    (void)out;(void)input;return RES_FAIL;
}

#ifdef DM_LINUX_GFX
/* Optional title-specific dynamic resolution is disabled for unreviewed 90447. */
void dm_android_scale3d_present(uint64_t presented_ns, uint64_t target_ns) { (void)presented_ns; (void)target_ns; }
void dm_3d_scale_resumed(void) {}
#endif
