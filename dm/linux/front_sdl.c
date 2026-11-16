// INPUT:  SDL video/controller events and Linux-rewritten game modules.
// OUTPUT:  A Linux Vulkan/GLES window, controller snapshots and present counts.
// POS:    Main-thread Linux front end; the original guest runs in a worker.
#include "../host/hle.h"
#include "../host/front.h"
#include "../android/input_backend.h"
#include "auto_classic_input.h"
extern int dm_linux_classic_target_mode(void);
extern int dm_linux_menu_scene_context(void);
extern void dm_linux_menu_scene_changed(void);
#include <SDL2/SDL.h>
#include "wait_trace.h"
#include <errno.h>
#include <SDL2/SDL_vulkan.h>
#ifdef DM_LINUX_GLES
#include "window_gles.h"
#define dm_linux_vulkan_private_requested() 0
#define dm_linux_vulkan_bind_window dm_linux_gles_bind_window
#define dm_linux_vulkan_drawable_size dm_linux_gles_drawable_size
#define dm_linux_vulkan_refresh_drawable_size dm_linux_gles_refresh_drawable_size
#define DM_FRONT_BACKEND "gles"
#else
#include "window_vulkan.h"
#define DM_FRONT_BACKEND "vulkan"
#endif
#include "audio_sdl.h"
#include "memory_guard.h"
#include "input_control.h"
#include "keyboard_sdl.h"
#include "exit_menu.h"
#include "display_size.h"
#include "../android/audio_aaudio.h"
#include <stdatomic.h>
#include <unistd.h>

static SDL_Window *window;
#ifdef DM_LINUX_GAME_94012
static uint64_t guest_extent;
uint64_t front_game_res(int docked) { (void)docked;return guest_extent; }
#endif
static _Atomic int input_ready, finished;
static _Atomic uint64_t presented;
#ifdef DM_LINUX_SOULKNIGHT
uint64_t soul_present_count(void) {return atomic_load(&presented);}
#endif
static int guest_result, guest_argc;
static char **guest_argv;
void *dm_android_native_window(void) { return window; }
static int source_ready(void *opaque) { (void)opaque; return atomic_load(&input_ready); }
double front_display_refresh(char *name, size_t capacity) {
    strlcpy(name, "SDL display", capacity);
    SDL_DisplayMode mode;
    return !SDL_GetCurrentDisplayMode(0, &mode) && mode.refresh_rate > 0 ? mode.refresh_rate : 60.0;
}
void dm_android_front_presented(uint64_t frame) {
    uint64_t count=atomic_fetch_add(&presented, 1)+1;
    if (guest_argv[2] && (frame <= 3 || frame % 60 == 0)) {
        LOG("LINUX_PRESENT frame=%llu", (unsigned long long)frame);
        char temporary[4096];
        if (snprintf(temporary,sizeof temporary,"%s.tmp",guest_argv[2])>=(int)sizeof temporary)
            die("Linux presentation receipt path too long");
        FILE *out=fopen(temporary,"w");
        if (!out) die("Linux presentation receipt open");
        fprintf(out,"{\"original_game_entry_executed\":true,\"" DM_FRONT_BACKEND "_present_callbacks\":%llu,\"render_backend\":\"" DM_FRONT_BACKEND "\","
            "\"main_build_id\":\"%s\",\"visual_correctness_verified\":false,\"gameplay_verified\":false}\n",
            (unsigned long long)count,g_mod[0].build_id);
        if (fclose(out) || rename(temporary,guest_argv[2])) die("Linux presentation receipt commit");
    }
}
void dm_host_shutdown(const char *why) {
#ifdef DM_LINUX_SOULKNIGHT
    int soul_oe_request_exit(void);if(!soul_oe_request_exit())return;
#endif
    LOG("LINUX_SHUTDOWN reason=%s presented=%llu", why, (unsigned long long)atomic_load(&presented));
    /* Keep the process-owned SDL window alive through termination. Do not
     * destroy its native handles while a guest render worker can access them. */
    _exit(0);
}
void dm_shot_written(const char *path, int ok) { LOG("LINUX_SCREENSHOT path=%s ok=%d", path, ok); }
int front_text_input(const char *title,const char *sub,const char *guide,
    const char *initial,int maxlen,char *text,int capacity) {
    (void)title;(void)sub;(void)guide;
    extern int g_swkbd_name_panel,g_swkbd_numeric_max;
    return dm_linux_keyboard_request(initial,maxlen,
        g_swkbd_name_panel||dm_linux_menu_scene_context()==5,g_swkbd_numeric_max,text,capacity);
}
void dm_ui_text_entered(const char *text) { LOG("LINUX_KEYBOARD returned_text=%d",text&&*text); }
static void *run_guest(void *opaque) {
    (void)opaque;
    int dm_linux_host_main(int, char **);
    guest_result = dm_linux_host_main(guest_argc, guest_argv);
    atomic_store(&finished, 1);
    return NULL;
}
static uint64_t button_bits(SDL_GameController *controller, const Uint8 *keys) {
    static const struct { SDL_GameControllerButton source; SDL_Scancode key; unsigned bit; } map[] = {
        {SDL_CONTROLLER_BUTTON_A,SDL_SCANCODE_RETURN,0}, {SDL_CONTROLLER_BUTTON_B,SDL_SCANCODE_ESCAPE,1},
        {SDL_CONTROLLER_BUTTON_X,SDL_SCANCODE_X,2}, {SDL_CONTROLLER_BUTTON_Y,SDL_SCANCODE_Y,3},
        {SDL_CONTROLLER_BUTTON_LEFTSTICK,SDL_SCANCODE_UNKNOWN,4}, {SDL_CONTROLLER_BUTTON_RIGHTSTICK,SDL_SCANCODE_UNKNOWN,5},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER,SDL_SCANCODE_Q,6}, {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,SDL_SCANCODE_E,7},
        {SDL_CONTROLLER_BUTTON_START,SDL_SCANCODE_TAB,10}, {SDL_CONTROLLER_BUTTON_BACK,SDL_SCANCODE_BACKSPACE,11},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT,SDL_SCANCODE_LEFT,12}, {SDL_CONTROLLER_BUTTON_DPAD_UP,SDL_SCANCODE_UP,13},
        {SDL_CONTROLLER_BUTTON_DPAD_RIGHT,SDL_SCANCODE_RIGHT,14}, {SDL_CONTROLLER_BUTTON_DPAD_DOWN,SDL_SCANCODE_DOWN,15}
    };
    uint64_t bits = 0;
    for (unsigned i=0; i<sizeof map/sizeof *map; ++i)
        if ((controller && SDL_GameControllerGetButton(controller,map[i].source)) ||
            (map[i].key != SDL_SCANCODE_UNKNOWN && keys[map[i].key])) bits |= 1ull << map[i].bit;
    if (controller && SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_TRIGGERLEFT)>8192) bits |= 1ull<<8;
    if (controller && SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_TRIGGERRIGHT)>8192) bits |= 1ull<<9;
    return bits;
}
int main(int argc, char **argv) {
#if defined(DM_SOULKNIGHT_PRODUCTION_BUILD) || defined(DM_SOULKNIGHT_GLES_EXPERIMENT)
#ifdef DM_SOULKNIGHT_PRODUCTION_BUILD
    if(argc!=2){fprintf(stderr,"usage: soulknight-ns NATIVE_DIRECTORY\n");return 2;}
#endif
    char *launch_argv[]={argv[0],argc>1?argv[1]:NULL,NULL,"game",NULL};
    if(argc==2){argv=launch_argv;argc=4;}
#endif
    dm_linux_memory_guard_init();
    if(argc==3&&!strcmp(argv[1],"--audio-probe")) {
        dm_android_audio_metrics stereo,surround;
        int first=dm_android_audio_probe_channels(2,&stereo);
        int second=first?first:dm_android_audio_probe_channels(6,&surround);
        if(first||second){fprintf(stderr,"LINUX_AUDIO_PROBE failed stereo=%d surround=%d\n",first,second);return 1;}
        FILE *out=fopen(argv[2],"w");if(!out)die("Linux audio probe result open");
        fprintf(out,"{\"passed\":true,\"native_audio_driver\":\"%s\",\"stereo_and_six_channel_queues_verified\":true,"
            "\"render_callbacks\":%llu,\"synthetic_probe_pcm_bytes_copied\":%llu,\"test_buffers_released\":%llu,"
            "\"game_entry_executed\":false,"
            "\"hardware_timestamp_verified\":false,\"speaker_audibility_verified\":false,\"original_sdk_events_verified\":false,\"game_frames\":0}\n",
            SDL_GetCurrentAudioDriver(),(unsigned long long)surround.render_callbacks,(unsigned long long)surround.game_bytes_copied,
            (unsigned long long)surround.buffers_released);
        if(fclose(out))die("Linux audio probe result close");return 0;
    }
    int display_probe = argc == 3 && !strcmp(argv[1], "--display-probe");
    int surface_probe = display_probe || (argc == 3 && !strcmp(argv[1], "--surface-probe"));
    if (!surface_probe && (argc != 4 || strcmp(argv[3],"game"))) {
        fprintf(stderr,"usage: d2r-linux PATCH_DIR RESULT_JSON game | --surface-probe RESULT_JSON | --display-probe RESULT_JSON\n"); return 2;
    }
    if (dm_linux_wait_trace_init()) die("Linux wait trace initialization: %s", strerror(errno));
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_EVENTS)) {
        fprintf(stderr,"LINUX_SDL_INIT_FAILED %s\n",SDL_GetError()); return 1;
    }
    LOG("LINUX_INPUT joysticks=%d first_controller_mapped=%d",SDL_NumJoysticks(),
        SDL_NumJoysticks()>0?SDL_IsGameController(0):0);
#ifdef DM_LINUX_GLES
    if(dm_linux_gles_configure())die("GLES context configuration: %s",SDL_GetError());
#endif
#ifdef DM_LINUX_SOULKNIGHT
    const char *window_title="元气骑士_ns";
#else
    const char *window_title=surface_probe?"D2R Vulkan display test (no game)":"Diablo II Resurrected — Linux bring-up";
#endif
    window = SDL_CreateWindow(window_title,SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,1280,720,SDL_WINDOW_SHOWN|SDL_WINDOW_FULLSCREEN_DESKTOP|
        (dm_linux_vulkan_private_requested() ? 0 :
#ifdef DM_LINUX_GLES
         SDL_WINDOW_OPENGL));
#else
         SDL_WINDOW_VULKAN));
#endif
    if (!window) { fprintf(stderr,"LINUX_SDL_WINDOW_FAILED %s\n",SDL_GetError()); SDL_Quit(); return 1; }
    /* Use the same Vulkan loader for SDL's surface and the replay backend. */
    if (!dm_linux_vulkan_bind_window(window)) die("Linux Vulkan window binding: %s",SDL_GetError());
    int pixel_width,pixel_height;dm_linux_vulkan_drawable_size(window,&pixel_width,&pixel_height);
    LOG("LINUX_WINDOW driver=%s pixels=%dx%d fullscreen_desktop=true",SDL_GetCurrentVideoDriver(),pixel_width,pixel_height);
#ifdef DM_LINUX_GAME_94012
    guest_extent=dm_linux_display_extent(pixel_width,pixel_height);
    if(!guest_extent)die("Linux drawable has no usable game extent");
    LOG("LINUX_GAME_EXTENT drawable=%dx%d guest=%ux%u handheld=docked aspect_preserved=1",pixel_width,pixel_height,
        (unsigned)guest_extent,(unsigned)(guest_extent>>32));
#endif
    const char *shaders=getenv("DM_SHADER_ROOT");
    if (!shaders || !*shaders) die("Set DM_SHADER_ROOT to the shaders translated from this game build");
    void dm_vk_set_shader_root(const char *);
    void dm_vk_set_window(void *);
    dm_vk_set_shader_root(shaders);
    dm_vk_set_window(window);
    if (surface_probe) {
        if (display_probe) { void dm_vk_linux_display_probe(void); dm_vk_linux_display_probe(); }
        FILE *out = fopen(argv[2], "w");
        if (!out) die("Linux surface probe result open");
        fprintf(out, "{\"passed\":true,\"render_backend\":\"" DM_FRONT_BACKEND "\",\"" DM_FRONT_BACKEND "_device_and_surface_created\":true,"
            "\"private_wayland_wsi\":%s,\"test_frames_presented\":%d,\"game_entry_executed\":false,\"game_frames\":0}\n",
            dm_linux_vulkan_private_requested() ? "true" : "false", display_probe ? 1 : 0);
        if (fclose(out)) die("Linux surface probe result close");
        LOG("LINUX_SURFACE_PROBE passed; game_frames=0");
#if defined(DM_LINUX_SOULKNIGHT) && defined(DM_LINUX_GLES)
        if(display_probe)printf("SOULKNIGHT_GLES=1\nPORTKIT_DISPLAY_WIDTH=%d\nPORTKIT_DISPLAY_HEIGHT=%d\n",pixel_width,pixel_height);
#endif
        if (display_probe) {
            Uint32 until=SDL_GetTicks()+3000;
            while ((Sint32)(until-SDL_GetTicks())>0) {
                SDL_Event event; while (SDL_PollEvent(&event)) {}
                SDL_Delay(10);
            }
        }
        /* Retain the SDL window through process termination while the backend
         * owns its native surface; no guest has been started by either probe. */
        return 0;
    }
    atomic_store(&input_ready,1);
    /* Horizon HID uses a fixed logical touch panel; normalized input is
     * independent of the game's render extent and the rotated SDL drawable. */
    dm_android_input_config input={.source="Linux SDL keyboard/gamecontroller",.panel_width=1280,.panel_height=720,
        .source_ready=source_ready,.logical_npad_id=0,.logical_npad_style=1};
    if (dm_android_input_bind(&input)) die("Linux SDL input binding");
    guest_argc=argc; guest_argv=argv;
    pthread_t thread;
    if (pthread_create(&thread,NULL,run_guest,NULL)) die("Linux guest worker");
    SDL_GameController *controller=NULL;
    const char *test_control=getenv("DM_LINUX_INPUT_CONTROL");
    dm_linux_test_input test_input={0};
    DmAutoClassicInput auto_classic={0};const char *auto_value=getenv("DM_LINUX_AUTO_CLASSIC");
    if(auto_value&&strcmp(auto_value,"0")&&strcmp(auto_value,"1"))die("auto classic requires 0 or 1");
    int auto_enabled=auto_value&&!strcmp(auto_value,"1");
    Uint32 status_next=0,auto_rearm_next=0,exit_check_next=0;int exit_ready=0;uint64_t exit_previous=0;
    Uint32 old_window_flags=~0u;uint64_t old_scene_input=0;Uint32 scene_input_started=0,scene_repeat_next=0;
    while (!atomic_load(&finished)) {
#ifdef DM_LINUX_SOULKNIGHT
        int soul_oe_exit_ready(void);if(soul_oe_exit_ready())dm_host_shutdown("guest exit handling complete or deadline");
#endif
        dm_linux_memory_guard_poll();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            dm_linux_audio_event(&event);
            if (event.type==SDL_QUIT) dm_host_shutdown("SDL quit");
            if(dm_linux_keyboard_event(&event))continue;
            if (event.type==SDL_WINDOWEVENT&&event.window.windowID==SDL_GetWindowID(window)) {
                dm_linux_vulkan_refresh_drawable_size(window);
                /* A Wayland surface may receive its first keyboard enter only
                 * after the first attached frame. Do not infer focus loss from
                 * a pre-map flag query and prevent the guest's first frame. */
                if(event.window.event==SDL_WINDOWEVENT_FOCUS_GAINED)dm_focus_set(1);
                else if(event.window.event==SDL_WINDOWEVENT_FOCUS_LOST)dm_focus_set(2);
                else if(event.window.event==SDL_WINDOWEVENT_MINIMIZED||event.window.event==SDL_WINDOWEVENT_HIDDEN)dm_focus_set(3);
            }
            if (event.type==SDL_WINDOWEVENT&&event.window.windowID==SDL_GetWindowID(window)&&
                event.window.event==SDL_WINDOWEVENT_FOCUS_LOST) dm_android_input_cancel();
        }
        if (controller && !SDL_GameControllerGetAttached(controller)) { SDL_GameControllerClose(controller); controller=NULL; }
        if (!controller) for (int i=0;i<SDL_NumJoysticks();++i) if (SDL_IsGameController(i)) {
            controller=SDL_GameControllerOpen(i);
            if(controller){
                char *mapping=SDL_GameControllerMapping(controller);
                LOG("LINUX_CONTROLLER opened name=%s mapping=%s start=plus select=minus",SDL_GameControllerName(controller),mapping?mapping:"unavailable");
                SDL_free(mapping);break;
            }
        }
        const Uint8 *keys=SDL_GetKeyboardState(NULL);
        static const Uint8 no_keys[SDL_NUM_SCANCODES]={0};
        int keyboard_was_active=dm_linux_keyboard_active();
        uint64_t buttons=button_bits(controller,keyboard_was_active?no_keys:keys);
        /* Drain physical keyboard releases as well as controller releases.
         * This bit is modal-only and is never published to the guest. */
        if(keyboard_was_active){for(unsigned i=0;i<SDL_NUM_SCANCODES;i++)if(keys[i]){buttons|=1ull<<63;break;}}
        int32_t lx=controller?SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_LEFTX):0;
        int32_t ly=controller?-(int32_t)SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_LEFTY):0;
        int32_t rx=controller?SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_RIGHTX):0;
        int32_t ry=controller?-(int32_t)SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_RIGHTY):0;
        if (ly>32767)ly=32767;if(ry>32767)ry=32767;
        Uint32 now=SDL_GetTicks();Uint32 flags=SDL_GetWindowFlags(window);
        if(flags!=old_window_flags || (test_control&&(Sint32)(now-status_next)>=0)) {
            LOG("LINUX_WINDOW_STATE focused=%d visible=%d flags=%#x physical_buttons=%#llx sticks=%d,%d,%d,%d",
                !!(flags&SDL_WINDOW_INPUT_FOCUS),!!(flags&SDL_WINDOW_SHOWN),flags,
                (unsigned long long)buttons,lx,ly,rx,ry);
            old_window_flags=flags;status_next=now+5000;
        }
        if(test_control&&*test_control)dm_linux_test_poll(&test_input,test_control,now,&buttons,&lx,&ly,&rx,&ry);
#ifdef DM_LINUX_SOULKNIGHT
        if((buttons&((1ull<<10)|(1ull<<11)))==((1ull<<10)|(1ull<<11)))dm_host_shutdown("Select+Start");
#endif
        extern int dm_linux_exit_menu_ready(void),dm_linux_try_menu_exit(void);
#if !defined(DM_LINUX_GLES) && !defined(DM_LINUX_SOULKNIGHT)
        if((Sint32)(now-exit_check_next)>=0||((buttons&~exit_previous)&3)){
            exit_ready=dm_linux_exit_menu_ready();exit_check_next=now+100;
        }
        exit_previous=buttons;
        int keyboard_owner=dm_linux_keyboard_active();
        int exit_action=dm_linux_exit_menu_tick(window,exit_ready&&!keyboard_owner&&!!(flags&SDL_WINDOW_INPUT_FOCUS),buttons,keyboard_owner);
        if(exit_action==2&&dm_linux_exit_menu_ready()){
            if(!dm_linux_try_menu_exit())dm_linux_exit_menu_saving();
        }
#else
        (void)exit_check_next;(void)exit_ready;(void)exit_previous;int exit_action=0;
#endif
        if(exit_action){buttons=0;lx=ly=rx=ry=0;}
        int keyboard_consumed=dm_linux_keyboard_tick(window,buttons,lx,ly,now)||exit_action;
        if(keyboard_consumed){buttons=0;lx=ly=rx=ry=0;}
        uint64_t scene_input=buttons|(uint64_t)(lx< -8192)<<48|(uint64_t)(lx>8192)<<49|
            (uint64_t)(ly< -8192)<<50|(uint64_t)(ly>8192)<<51;
        if(scene_input&~old_scene_input){int context=dm_linux_menu_scene_context();
            scene_input_started=now;scene_repeat_next=now+300;
            if(context==4||context==5)dm_linux_menu_scene_changed();}
        /* The guest repeats held navigation without another physical edge.
         * Refresh periodically and once after a long hold ends. */
        if(scene_input&&(Sint32)(now-scene_repeat_next)>=0){
            int context=dm_linux_menu_scene_context();scene_repeat_next=now+200;
            if(context==4||context==5)dm_linux_menu_scene_changed();
        }
        if(old_scene_input&&!scene_input&&(Uint32)(now-scene_input_started)>=250){
            int context=dm_linux_menu_scene_context();
            if(context==4||context==5)dm_linux_menu_scene_changed();
        }
        old_scene_input=scene_input;
        if(abs(rx)>8192||abs(ry)>8192)dm_linux_menu_scene_changed();
        if(!keyboard_consumed&&auto_enabled&&(!auto_classic.done||(Sint32)(now-auto_rearm_next)>=0)){int activity=buttons||abs(lx)>8192||abs(ly)>8192||abs(rx)>8192||abs(ry)>8192;unsigned was_done=auto_classic.done;
            /* A completed one-shot only needs a low-rate menu return check.
             * Physical input is published every loop independently. */
            if(was_done)auto_rearm_next=now+250;
            dm_android_input_status sampled;dm_android_input_status_get(&sampled);
            if(dm_auto_classic_input(&auto_classic,dm_linux_classic_target_mode(),now,activity,
                (uint64_t)sampled.returned_state.sampling,sampled.returned_state.buttons,&buttons))
                LOG("LINUX_AUTO_CLASSIC requested original_input=1 process_only=1");
            if(!was_done&&auto_classic.done)LOG("LINUX_AUTO_CLASSIC reached original_target=classic stopped=1");
            if(was_done&&!auto_classic.done)LOG("LINUX_AUTO_CLASSIC rearmed verified_main_menu=1 input_emitted=0");
        }
        int error=dm_android_input_publish_controller_actions(buttons,lx,ly,rx,ry);
        if (error) die("Linux SDL input publish: %d",error);
        SDL_Delay(5);
    }
    pthread_join(thread,NULL);
    if(controller)SDL_GameControllerClose(controller);
    SDL_DestroyWindow(window); SDL_Quit();
    return guest_result;
}
