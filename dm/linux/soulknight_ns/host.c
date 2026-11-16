// INPUT: Matching Linux-rewritten Soul Knight NS 1.2.0 modules.
// OUTPUT: Original SDK initialization followed by the original Unity nnMain.
// POS: Title bootstrap; shared loading, TLS, services and graphics stay shared.
#include "../../host/dm.h"
#include "runtime_build.h"

struct dm_flags g_flag;
uint64_t g_t0_ns;
static pthread_t main_thread;
int soul_on_main_thread(void) {return pthread_equal(pthread_self(),main_thread);}
static void *required(const char *name) {
    void *result=guest_sym(name);
    if(!result)die("Soul Knight original SDK symbol missing: %s",name);
    return result;
}
int dm_linux_host_main(int argc,char **argv) {
    if(argc!=4||strcmp(argv[3],"game"))return 2;
    main_thread=pthread_self();g_t0_ns=now_ns();
    void crash_thread_init(void);crash_thread_init();
    int diagnostics=getenv("SOULKNIGHT_DIAGNOSTICS")&&!strcmp(getenv("SOULKNIGHT_DIAGNOSTICS"),"1");
    g_flag.imports=diagnostics&&getenv("DM_IMPORTS");
    g_flag.svc=diagnostics&&getenv("DM_SVC");g_flag.dbg=diagnostics;
    void dm_android_svc_set_program_id(uint64_t);
    dm_android_svc_set_program_id(0x0100a0800b83a000ull);
    /* Strong global definitions precede SDK weak allocation operators. The
     * loader binds hidden/protected/local definitions to their own module. */
    const char *names[]={"main","subsdk0","sdk"};
    const char *ids[]={"02242ae4e90dcf439210fe87f692de05","5a7ada8dd7447c10aa244944f64d0aff","ad3fdfbd62a5f4b082d385ffe420d440"};
    load_modules(argv[1],names,3);
    if(g_nmod!=3)die("Soul Knight module count differs");
    for(unsigned i=0;i<3;i++)if(strcmp(g_mod[i].name,names[i])||strcmp(g_mod[i].build_id,ids[i]))
        die("Soul Knight module identity differs: %s",names[i]);
    void soul_aspect_prepare(void);soul_aspect_prepare();
    void guest_thread_init(void);guest_thread_init();
    uint32_t svc_thread_main_init(void);uint32_t handle=svc_thread_main_init();
    ((void (*)(void))required("_init_libc0"))();
    ((void (*)(uint64_t,uint64_t))required("nnosInitialize"))(handle,0);
    ((void (*)(void))required("_init_libc1"))();
    ((void (*)(void))required("nninitInitializeSdkModule"))();
    ((void (*)(void))required("nninitStartup"))();
    ((void (*)(void))required("_init_libc2"))();
    run_module_inits();
    char *game_argv[]={"SoulKnightNS",NULL};
    ((void (*)(int))required("_ZN2nn2os11SetHostArgcEi"))(1);
    ((void (*)(char **))required("_ZN2nn2os11SetHostArgvEPPc"))(game_argv);
    ((void (*)(void))required("nnMain"))();
    die("Soul Knight original nnMain unexpectedly returned");
}
