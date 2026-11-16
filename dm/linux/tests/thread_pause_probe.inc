/* AArch64 hardware fixture: exercise real SVC handles, signal/context state,
 * a blocked SVC waking while paused, resume and native join before stack reuse. */
typedef struct { _Atomic unsigned finish,word;_Atomic uint64_t counter; } soul_pause_fixture;
extern char __start_soul_pause_guest[],__stop_soul_pause_guest[];
__attribute__((noinline,section("soul_pause_guest"))) static void soul_pause_busy(uint64_t argument) {
    soul_pause_fixture *state=(void *)(uintptr_t)argument;
    dm_linux_slot_set(g_key18,(void *)0x1818);
    __asm__ volatile("mov x19,#0x1919\n\tmovi v8.16b,#0x5a\n"
        "1: ldr w9,[%[finish]]\n\tcbnz w9,2f\n\tldr x9,[%[counter]]\n\tadd x9,x9,#1\n\tstr x9,[%[counter]]\n\tb 1b\n2:"
        ::[finish]"r"(&state->finish),[counter]"r"(&state->counter):"x9","x19","v8","memory");
    thread_exit(cur());
}
static void soul_pause_blocked(uint64_t argument) {
    soul_pause_fixture *state=(void *)(uintptr_t)argument;
    _Alignas(16) uint64_t frame[102]={0};frame[19]=0x1919;frame[100]=0x2929;frame[101]=0x3030;
    memset((uint8_t *)frame+256+8*16,0x5a,16);
    dm_linux_slot_set(g_key18,(void *)0x1818);
    soul_svc_enter(frame,(uintptr_t)soul_pause_busy);
    frame[0]=(uintptr_t)&state->word;frame[1]=2;frame[2]=0;frame[3]=UINT64_MAX;
    if(!svc_thread(0x34,frame) || frame[0])die("blocked pause fixture wait");
    soul_svc_leave();atomic_store(&state->counter,1);thread_exit(cur());
}
static void soul_pause_sleeping(uint64_t argument) {
    soul_pause_fixture *state=(void *)(uintptr_t)argument;_Alignas(16) uint64_t frame[102]={200000000};
    soul_svc_enter(frame,(uintptr_t)soul_pause_busy);uint64_t begin=now_ns();
    if(!svc_thread(0x0b,frame) || now_ns()-begin<200000000)die("pause shortened kernel sleep");
    soul_svc_leave();atomic_store(&state->counter,1);thread_exit(cur());
}
static uint32_t soul_pause_spawn(void (*entry)(uint64_t),soul_pause_fixture *state,void **stack) {
    *stack=mmap(NULL,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(*stack==MAP_FAILED)die("pause fixture stack");
    uint64_t r[31]={0,(uintptr_t)entry,(uintptr_t)state,(uintptr_t)*stack+65536,44,0};
    if(!svc_thread(8,r) || r[0])die("pause fixture create");uint32_t handle=r[1];
    r[0]=handle;if(!svc_thread(9,r) || r[0])die("pause fixture start");return handle;
}
static void soul_pause_wait_counter(soul_pause_fixture *state,uint64_t previous) {
    uint64_t deadline=now_ns()+1000000000ull;
    while(atomic_load(&state->counter)==previous){if(now_ns()>deadline)die("pause fixture resume progress");sched_yield();}
}
static void soul_pause_join(uint32_t handle,void *stack) {
    uint64_t deadline=now_ns()+1000000000ull;
    for(;;){pthread_mutex_lock(&G);thr_t *t=h_peek(handle,O_THREAD);int exited=t&&t->exited;pthread_mutex_unlock(&G);
        if(exited)break;if(now_ns()>deadline)die("pause fixture exit");sched_yield();}
    uint64_t r[31]={handle};if(!svc_thread(0x16,r) || r[0])die("pause fixture close/join");
    munmap(stack,65536);
}
void soul_thread_pause_check(void) {
    void dm_android_loader_keys_init(void),guest_thread_init(void),crash_thread_init(void);
    if(g_nmod)die("pause fixture requires no game modules");
    dm_android_loader_keys_init();crash_thread_init();guest_thread_init();svc_thread_main_init();
    g_mod[0].base=(uint8_t *)__start_soul_pause_guest;
    g_mod[0].size=(uintptr_t)__stop_soul_pause_guest-(uintptr_t)__start_soul_pause_guest;g_nmod=1;
    soul_pause_fixture state={0};void *stack;uint32_t handle=soul_pause_spawn(soul_pause_busy,&state,&stack);
    soul_pause_wait_counter(&state,0);_Alignas(16) uint8_t context[0x320];
    for(unsigned cycle=0;cycle<64;cycle++) {
        if(soul_thread_activity(handle,1) || soul_thread_context(handle,context))die("pause fixture suspend/context");
        uint64_t *x=(uint64_t *)context,previous=atomic_load(&state.counter);
        if(x[18]!=0x1818 || x[19]!=0x1919 || x[31]<(uintptr_t)stack || x[31]>=(uintptr_t)stack+65536 || !soul_guest_pc(x[32]))die("pause fixture GPR/SP/PC snapshot");
        for(unsigned b=0;b<16;b++)if(context[0x110+8*16+b]!=0x5a)die("pause fixture SIMD snapshot");
        struct timespec delay={0,1000000};nanosleep(&delay,NULL);
        if(atomic_load(&state.counter)!=previous)die("paused guest kept running");
        if(soul_thread_activity(handle,0))die("pause fixture resume");soul_pause_wait_counter(&state,previous);
    }
    /* No intervening guest progress: stress generation handoff rather than
     * relying on the counter loop to clear the previous signal's ack. */
    for(unsigned cycle=0;cycle<256;cycle++) {
        if(soul_thread_activity(handle,1) || soul_thread_context(handle,context) || soul_thread_activity(handle,0))die("pause fixture immediate re-pause");
    }
    atomic_store(&state.finish,1);soul_pause_join(handle,stack);
    state=(soul_pause_fixture){0};handle=soul_pause_spawn(soul_pause_blocked,&state,&stack);
    uint64_t deadline=now_ns()+1000000000ull;
    for(;;){pthread_mutex_lock(&G);thr_t *t=h_peek(handle,O_THREAD);int waiting=t&&t->wkind;pthread_mutex_unlock(&G);
        if(waiting)break;if(now_ns()>deadline)die("pause fixture blocked entry");sched_yield();}
    if(soul_thread_activity(handle,1) || soul_thread_context(handle,context))die("blocked fixture suspend/context");
    uint64_t *x=(uint64_t *)context;
    if(x[18]!=0x1818 || x[19]!=0x1919 || x[29]!=0x2929 || x[30]!=0x3030 || x[32]!=(uintptr_t)soul_pause_busy+4)die("blocked fixture gate snapshot");
    uint64_t r[31]={(uintptr_t)&state.word,0,0,1};svc_thread(0x35,r);
    struct timespec delay={0,2000000};nanosleep(&delay,NULL);
    if(atomic_load(&state.counter))die("blocked guest escaped pause after wake");
    if(soul_thread_context(handle,context) || ((uint64_t *)context)[0]!=(uintptr_t)&state.word)die("blocked result registers changed while paused");
    if(soul_thread_activity(handle,0))die("blocked fixture resume");soul_pause_wait_counter(&state,0);soul_pause_join(handle,stack);
    state=(soul_pause_fixture){0};handle=soul_pause_spawn(soul_pause_sleeping,&state,&stack);
    deadline=now_ns()+1000000000ull;
    for(;;){pthread_mutex_lock(&G);thr_t *t=h_peek(handle,O_THREAD);int sleeping=t&&t->sleep_safe;pthread_mutex_unlock(&G);
        if(sleeping)break;if(now_ns()>deadline)die("pause fixture sleep admission");sched_yield();}
    if(soul_thread_activity(handle,1) || soul_thread_context(handle,context) || soul_thread_activity(handle,0))die("sleeping fixture pause/context/resume");
    soul_pause_wait_counter(&state,0);soul_pause_join(handle,stack);
    g_nmod=0;memset(&g_mod[0],0,sizeof g_mod[0]);
    LOG("SOUL_THREAD_PAUSE_CHECK passed 64 progress +256 immediate signal/context cycles, GPR/SIMD/virtualTLS, blocked gate/result stability, kernel sleep deadline, native join");
}
