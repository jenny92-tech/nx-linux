// INPUT:  Linux-rewritten NSO modules and the shared ELF loader/gate implementation.
// OUTPUT:  Original SDK/nnMain execution, thread checks and bring-up receipts.
// POS:    Common guest bootstrap for CPU probes and the SDL/Vulkan executable.
#include "../host/dm.h"
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#include "../android/input_backend.h"
static int headless_input_ready(void *opaque) { (void)opaque; return 1; }

struct dm_flags g_flag;
uint64_t g_t0_ns;
static uint64_t (*original_tick)(void);
static void *(*original_thread)(void);
static unsigned workers_checked;

#ifndef DM_LINUX_GFX
uint64_t front_game_res(int docked) { (void)docked; return (720ull << 32) | 1280; }
uint64_t dm_game_res(int docked) { return front_game_res(docked); }
void *hlecount_wrap(const char *type, const char *name, void *fn) { (void)type; (void)name; return fn; }
void hlecount_seal(void) {}
#endif
static void signal_failure(int number, siginfo_t *info, void *opaque) {
    ucontext_t *u = opaque;
    dprintf(STDERR_FILENO, "LINUX_GUEST_SIGNAL signal=%d address=%p pc=%#llx lr=%#llx sp=%#llx\n",
        number, info->si_addr, (unsigned long long)u->uc_mcontext.pc,
        (unsigned long long)u->uc_mcontext.regs[30], (unsigned long long)u->uc_mcontext.sp);
    for (int i = 0; i < g_nmod; ++i)
        dprintf(STDERR_FILENO, "module=%s base=%p size=%zu\n", g_mod[i].name, g_mod[i].base, g_mod[i].size);
    for (unsigned i = 0; i < 30; i += 3)
        dprintf(STDERR_FILENO, "x%u=%#llx x%u=%#llx x%u=%#llx\n",i,(unsigned long long)u->uc_mcontext.regs[i],
            i+1,(unsigned long long)u->uc_mcontext.regs[i+1],i+2,(unsigned long long)u->uc_mcontext.regs[i+2]);
    _exit(128 + number);
}
static void *required(const char *name) {
    void *address = guest_sym(name);
    if (!address) die("Original SDK symbol missing: %s", name);
    return address;
}
static void check_original(void) {
    uintptr_t *slots = dm_linux_slots();
    uint64_t tick = original_tick();
    if (!tick) die("Original SDK counter returned zero");
    void *expected = *(void **)((uint8_t *)slots[3] + 0x1f8);
    if (original_thread() != expected) die("Original SDK TLR lookup differs from guest context");
}
static void *worker(void *argument) {
    void guest_thread_init(void);
    uintptr_t host_tp = (uintptr_t)__builtin_thread_pointer();
    uint64_t offset = dm_linux_tls_offset();
    guest_thread_init();
    dm_linux_slot_set(g_key18, argument);
    errno = E2BIG;
    check_original();
    if ((uintptr_t)__builtin_thread_pointer() != host_tp || dm_linux_tls_offset() != offset ||
        errno != E2BIG || dm_linux_slots()[1] != (uintptr_t)argument)
        die("Linux guest execution corrupted host TLS");
    return argument;
}
int dm_linux_host_main(int argc, char **argv) {
    if (argc != 3 && argc != 4) { fprintf(stderr, "usage: d2r-linux-host-probe PATCH_DIR RESULT_JSON [load|os-init|os-services|sdk-startup|game]\n"); return 2; }
    const char *mode = argc == 4 ? argv[3] : "load";
    if (strcmp(mode,"load") && strcmp(mode,"os-init") && strcmp(mode,"os-services") && strcmp(mode,"sdk-startup") && strcmp(mode,"game")) return 2;
    void crash_thread_init(void);
    crash_thread_init();
#ifndef DM_LINUX_GFX
    struct sigaction action = {.sa_sigaction = signal_failure, .sa_flags = SA_SIGINFO | SA_ONSTACK};
    sigemptyset(&action.sa_mask);
    for (unsigned i = 0; i < 4; ++i) {
        const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGABRT};
        if (sigaction(signals[i], &action, NULL)) die("Linux signal setup");
    }
#endif
    g_t0_ns = now_ns();
    g_flag.imports = getenv("DM_IMPORTS") != NULL;
    g_flag.svc = getenv("DM_SVC") != NULL;
    g_flag.dbg = getenv("DM_DBG") != NULL;
    g_flag.movie = getenv("DM_MOVIE") != NULL;
    const char *names[] = {"main", "sdk", "subsdk0"};
    load_modules(argv[1], names, 3);
    void guest_thread_init(void);
    guest_thread_init();
    original_tick = required("_ZN2nn2os13GetSystemTickEv");
    original_thread = required("_ZN2nn2os16GetCurrentThreadEv");
    check_original();
    uint64_t first = original_tick();
    struct timespec delay = {.tv_nsec = 2000000};
    nanosleep(&delay, NULL);
    uint64_t last = original_tick();
    if (last <= first || last - first < 10000) die("Original SDK clock failed to advance");
    dm_linux_slot_set(g_key18, (void *)0x1234);
    for (uintptr_t i = 1; i <= 4; ++i) {
        pthread_t thread; void *result = NULL;
        if (pthread_create(&thread, NULL, worker, (void *)i) || pthread_join(thread, &result) || result != (void *)i)
            die("Linux guest worker failed");
        if (dm_linux_slots()[1] != 0x1234) die("Linux guest threads share TLS slots");
        ++workers_checked;
    }
    if (strcmp(mode, "load")) {
        uint32_t svc_thread_main_init(void);
        uint32_t handle = svc_thread_main_init();
        LOG("LINUX_ORIGINAL _init_libc0");
        ((void (*)(void))required("_init_libc0"))();
        LOG("LINUX_ORIGINAL nnosInitialize");
        ((void (*)(uint64_t,uint64_t))required("nnosInitialize"))(handle,0);
        ((void (*)(void))required("_init_libc1"))();
        LOG("LINUX_ORIGINAL OS initialization returned");
    }
    if (!strcmp(mode,"os-services")) { void dm_linux_os_services_check(void); dm_linux_os_services_check(); }
    if (!strcmp(mode,"sdk-startup") || !strcmp(mode,"game")) {
        LOG("LINUX_ORIGINAL nninitInitializeSdkModule");
        ((void (*)(void))required("nninitInitializeSdkModule"))();
        LOG("LINUX_ORIGINAL nninitStartup");
        ((void (*)(void))required("nninitStartup"))();
        ((void (*)(void))required("_init_libc2"))();
    }
    if (!strcmp(mode,"game")) {
#ifndef DM_LINUX_GFX
        dm_android_input_config input = {.source="Linux headless, no controller", .panel_width=1280, .panel_height=720, .source_ready=headless_input_ready};
        if (dm_android_input_bind(&input)) die("Linux headless input configuration");
#endif
        run_module_inits();
        char *game_argv[] = {"D2R", NULL};
        ((void (*)(int))required("_ZN2nn2os11SetHostArgcEi"))(1);
        ((void (*)(char **))required("_ZN2nn2os11SetHostArgvEPPc"))(game_argv);
        LOG("LINUX_ORIGINAL nnMain");
        ((void (*)(void))required("nnMain"))();
        die("Original nnMain unexpectedly returned");
    }
    FILE *out = fopen(argv[2], "w");
    if (!out) die("Linux result open");
    fprintf(out, "{\"passed\":true,\"platform\":\"linux-aarch64-glibc\",\"original_sdk_executed\":true,"
        "\"game_entry_executed\":false,\"game_frames\":0,\"workers_checked\":%u,"
        "\"host_tls_preserved\":true,\"linux_tls_offset\":%llu,\"main_build_id\":\"%s\",\"tick_delta\":%llu,\"mode\":\"%s\","
        "\"original_tiny_stack_threads\":%u,\"original_event_cycles\":%u}\n",
        workers_checked, (unsigned long long)dm_linux_tls_offset(), g_mod[0].build_id,
        (unsigned long long)(last - first), mode, !strcmp(mode,"os-services") ? 32u : 0u, !strcmp(mode,"os-services") ? 3u : 0u);
    if (fclose(out)) die("Linux result close");
    LOG("LINUX_ORIGINAL_SDK passed clock/TLR, workers=%u, game_frames=0", workers_checked);
    return 0;
}

#ifndef DM_LINUX_GFX
int main(int argc, char **argv) { return dm_linux_host_main(argc, argv); }
#endif
