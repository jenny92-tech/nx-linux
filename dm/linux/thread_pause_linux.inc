/* Linux SetThreadActivity/GetThreadContext3, adapted from the pinned S2Arm
 * thread.c pause protocol. Never suspend host code: a signal parks only guest
 * PCs, while SVC waits are acknowledged under G and park before gate return.
 * GC needs a stable register image and stack, not a successful no-op. */
#include <stdatomic.h>
#include <ucontext.h>
#include <asm/sigcontext.h>
#include <linux/futex.h>
#include <sys/syscall.h>
_Static_assert(ATOMIC_INT_LOCK_FREE==2,"pause handler requires lock-free atomics");
static _Thread_local thr_t *soul_current_thread;
static pthread_cond_t soul_pause_cv=PTHREAD_COND_INITIALIZER;
static pthread_once_t soul_pause_once=PTHREAD_ONCE_INIT;
static void thr_free(thr_t *);

/* load_modules publishes this map once before any guest threads start.
 * Dynamic module/JIT loading is not implemented. The standalone probe only
 * changes its synthetic range before start and after all workers are joined. */
static int soul_guest_pc(uint64_t pc) {
    for(int i=0;i<g_nmod;i++)if(pc>=(uintptr_t)g_mod[i].base && pc-(uintptr_t)g_mod[i].base<g_mod[i].size)return 1;
    return 0;
}
static void soul_pause_signal(int number,siginfo_t *info,void *opaque) {
    (void)number;(void)info;
    int saved_errno=errno;thr_t *t=soul_current_thread;ucontext_t *context=opaque;
    if(t && atomic_load(&t->pause) && !atomic_load(&t->in_svc) && soul_guest_pc(context->uc_mcontext.pc)) {
        t->pause_context=*context;
        atomic_store(&t->pause_kind,2);
        do {
            while(atomic_load_explicit(&t->pause,memory_order_acquire)) {
                unsigned epoch=atomic_load_explicit(&t->pause_epoch,memory_order_acquire);
                if(!atomic_load(&t->pause))break;
                atomic_store(&t->ack_epoch,epoch);
                atomic_store_explicit(&t->pause_ack,1,memory_order_release);
                /* Wait on the generation, so resume followed immediately by
                 * another pause cannot lose its wake or accept an old ack. */
                syscall(SYS_futex,&t->pause_epoch,FUTEX_WAIT_PRIVATE,epoch,NULL,NULL,0);
            }
            /* Clear before becoming runnable. A new request racing resume
             * either reuses this still-parked image or waits for a new ack. */
            atomic_store_explicit(&t->pause_ack,0,memory_order_release);
        } while(atomic_load_explicit(&t->pause,memory_order_acquire));
        atomic_store(&t->pause_kind,0);
    }
    errno=saved_errno;
}
static void soul_pause_initialize(void) {
    struct sigaction previous;
    if(sigaction(SIGURG,NULL,&previous) || (previous.sa_handler!=SIG_DFL && previous.sa_handler!=SIG_IGN))
        die("Linux pause signal already owned");
    struct sigaction action={.sa_sigaction=soul_pause_signal,.sa_flags=SA_SIGINFO|SA_ONSTACK|SA_RESTART};
    sigemptyset(&action.sa_mask);
    if(sigaction(SIGURG,&action,NULL))die("Linux pause signal installation");
}
static void soul_pause_attach(thr_t *t) {
    pthread_once(&soul_pause_once,soul_pause_initialize);
    soul_current_thread=t;t->guest_slots=dm_linux_slots();
    sigset_t signals;sigemptyset(&signals);sigaddset(&signals,SIGURG);
    if(pthread_sigmask(SIG_UNBLOCK,&signals,NULL))die("Linux pause signal unblock");
}
static void soul_pause_park_locked(thr_t *t) {
    while(atomic_load(&t->pause)) {
        t->pause_wait=0;atomic_store(&t->pause_kind,1);atomic_store(&t->ack_epoch,atomic_load(&t->pause_epoch));
        atomic_store_explicit(&t->pause_ack,1,memory_order_release);
        pthread_cond_wait(&soul_pause_cv,&G);
    }
    atomic_store_explicit(&t->pause_ack,0,memory_order_release);atomic_store(&t->pause_kind,0);
}
/* A woken or timed-out SVC must park before it writes result registers.
 * Otherwise a virtually paused gate frame can race GetThreadContext3. G is
 * held at every block return, so this also closes the wake/ack transition. */
static int64_t soul_pause_block_return_locked(thr_t *t,int64_t result) {
    soul_pause_park_locked(t);return result;
}
static void soul_pause_sleep_safe(int enter) {
    thr_t *t=soul_current_thread;if(!t)return;
    pthread_mutex_lock(&G);if(!enter)soul_pause_park_locked(t);
    t->sleep_safe=enter;pthread_mutex_unlock(&G);
}
void soul_svc_enter(uint64_t *registers,uint64_t pc) {
    thr_t *t=soul_current_thread;if(!t)return;
    pthread_mutex_lock(&G);t->svc_regs=registers;t->svc_pc=pc;
    atomic_store(&t->in_svc,1);soul_pause_park_locked(t);pthread_mutex_unlock(&G);
}
void soul_svc_leave(void) {
    thr_t *t=soul_current_thread;if(!t)return;
    pthread_mutex_lock(&G);soul_pause_park_locked(t);
    t->svc_regs=NULL;atomic_store(&t->in_svc,0);pthread_mutex_unlock(&G);
}
static uint32_t soul_thread_activity(uint32_t handle,unsigned activity) {
    pthread_mutex_lock(&G);thr_t *t=h_peek(handle,O_THREAD);
    uint32_t result=!t?R_BAD_HANDLE:t==soul_current_thread?0xF401u:!t->started||t->exited||activity>1?R_BAD_STATE:0;
    if(result){pthread_mutex_unlock(&G);return result;}
    if(atomic_load(&t->pause)==(int)activity){pthread_mutex_unlock(&G);return R_BAD_STATE;}
    ++t->pins;
    if(!activity) {
        if(t->pause_wait){t->pause_wait=0;atomic_store(&t->pause_ack,0);atomic_store(&t->pause_kind,0);}
        atomic_store_explicit(&t->pause,0,memory_order_release);
        atomic_fetch_add(&t->pause_epoch,1);
        syscall(SYS_futex,&t->pause_epoch,FUTEX_WAKE_PRIVATE,INT_MAX,NULL,NULL,0);
        pthread_cond_broadcast(&soul_pause_cv);
    } else {
        atomic_store(&t->pause,1);atomic_fetch_add(&t->pause_epoch,1);
        syscall(SYS_futex,&t->pause_epoch,FUTEX_WAKE_PRIVATE,INT_MAX,NULL,NULL,0);
    }
    unsigned requested_epoch=atomic_load(&t->pause_epoch);
    pthread_mutex_unlock(&G);
    uint64_t deadline=now_ns()+2000000000ull;
    while(activity) {
        pthread_mutex_lock(&G);
        if(t->exited || !atomic_load(&t->pause) || atomic_load(&t->pause_epoch)!=requested_epoch)result=R_BAD_STATE;
        else if(atomic_load_explicit(&t->pause_ack,memory_order_acquire) && atomic_load(&t->ack_epoch)==requested_epoch) {
            pthread_mutex_unlock(&G);break;
        } else if(atomic_load(&t->in_svc) && (t->wkind || t->sleep_safe) && t->svc_regs) {
            /* The target released G inside the SVC's CV wait. It cannot
             * return to guest code without soul_svc_leave acquiring G. */
            t->pause_wait=1;atomic_store(&t->pause_kind,1);atomic_store(&t->ack_epoch,requested_epoch);
            atomic_store_explicit(&t->pause_ack,1,memory_order_release);
            pthread_mutex_unlock(&G);break;
        } else if(now_ns()>=deadline)result=R_TIMEOUT;
        else if(pthread_kill(t->pt,SIGURG))result=R_BAD_STATE;
        if(result && atomic_load(&t->pause_epoch)==requested_epoch) {
            atomic_store(&t->pause,0);pthread_cond_broadcast(&soul_pause_cv);
            atomic_fetch_add(&t->pause_epoch,1);
        syscall(SYS_futex,&t->pause_epoch,FUTEX_WAKE_PRIVATE,INT_MAX,NULL,NULL,0);
        }
        pthread_mutex_unlock(&G);
        if(result)break;
        struct timespec delay={0,100000};nanosleep(&delay,NULL);
    }
    pthread_mutex_lock(&G);
    if(!--t->pins && t->exited && t->closed)thr_free(t);
    pthread_mutex_unlock(&G);return result;
}
static uint32_t soul_thread_context(uint32_t handle,uint8_t *destination) {
    uint8_t out[0x320]={0};uint64_t x[31],sp,pc;uint32_t pstate,fpsr,fpcr;__uint128_t vectors[32];
    pthread_mutex_lock(&G);thr_t *t=h_peek(handle,O_THREAD);
    uint32_t result=!t?R_BAD_HANDLE:t==soul_current_thread?0xF401u:
        t->exited || !atomic_load(&t->pause) || !atomic_load_explicit(&t->pause_ack,memory_order_acquire) || atomic_load(&t->ack_epoch)!=atomic_load(&t->pause_epoch)?R_BAD_STATE:0;
    if(!result) {
        if(atomic_load(&t->pause_kind)==2) {
            const mcontext_t *context=&t->pause_context.uc_mcontext;
            memcpy(x,context->regs,sizeof x);sp=context->sp;pc=context->pc;pstate=context->pstate;
            const struct fpsimd_context *fp=NULL;
            for(size_t at=0;at+sizeof(struct _aarch64_ctx)<=sizeof context->__reserved;) {
                const struct _aarch64_ctx *record=(const void *)(context->__reserved+at);
                if(!record->magic || record->size<sizeof *record || record->size>sizeof context->__reserved-at)break;
                if(record->magic==FPSIMD_MAGIC && record->size==sizeof *fp){fp=(const void *)record;break;}
                at+=record->size;
            }
            if(!fp)die("Linux paused context lacks FPSIMD");
            memcpy(vectors,fp->vregs,sizeof vectors);fpsr=fp->fpsr;fpcr=fp->fpcr;
        } else if(t->svc_regs) {
            const uint64_t *r=t->svc_regs;
            /* This port's gate differs from current S2Arm's gate. entry.S
             * saves 800 bytes; site_gate saved original FP/LR immediately
             * above that frame. There is no pointer at r[100]. */
            memcpy(x,r,29*8);x[29]=r[100];x[30]=r[101];sp=(uintptr_t)r+816;pc=t->svc_pc+4;
            memcpy(vectors,(const uint8_t *)r+256,sizeof vectors);pstate=r[96];fpsr=r[97];fpcr=r[98];
        } else die("Linux pause acknowledged without a stable context");
        x[18]=t->guest_slots[1];memcpy(out,x,sizeof x);memcpy(out+0xf8,&sp,8);memcpy(out+0x100,&pc,8);
        pstate&=0xFF0FFE20;memcpy(out+0x108,&pstate,4);memcpy(out+0x110,vectors,sizeof vectors);
        memcpy(out+0x310,&fpcr,4);memcpy(out+0x314,&fpsr,4);memcpy(out+0x318,&t->guest_slots[3],8);
    }
    pthread_mutex_unlock(&G);if(!result)memcpy(destination,out,sizeof out);return result;
}
