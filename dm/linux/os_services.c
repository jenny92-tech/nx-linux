// INPUT: Original SDK thread/event exports and Linux Horizon-kernel adapters.
// OUTPUT: Real 4 KiB thread teardown, blocking wake-up and resource-leak checks.
// POS: Validate guest/host stack ownership before relying on game threads.
#include "../host/hle.h"
#include <stdatomic.h>
#include <time.h>
void *dm_guest_thread_spawn(void (*)(void *), void *, int, size_t);
void dm_guest_thread_join(void *);
void thr_counts(int *, int *, int *);
static void *required(const char *name) { void *p=guest_sym(name); if(!p)die("Original SDK missing %s",name); return p; }
static uint64_t svc_count(unsigned number) {
#ifdef DM_LINUX_GFX
    uint64_t dm_android_svc_count(int); return dm_android_svc_count((int)number);
#else
    uint64_t dm_linux_svc_count(unsigned); return dm_linux_svc_count(number);
#endif
}
static void (*event_wait)(void *);
static _Atomic int worker_started, worker_done;
static _Atomic uint64_t worker_wait_ns;
static _Atomic unsigned tiny_stack_returns;
static void tiny_stack_worker(void *arg) { (void)arg;atomic_fetch_add(&tiny_stack_returns,1); }
static void event_worker(void *event) {
    uint64_t (*tick)(void)=required("_ZN2nn2os13GetSystemTickEv");
    if(!tick())die("Original SDK worker counter");
    uint64_t start=now_ns(); atomic_store(&worker_started,1);
    event_wait(event); atomic_store(&worker_wait_ns,now_ns()-start); atomic_store(&worker_done,1);
}
void dm_linux_os_services_check(void) {
    for(unsigned i=0;i<32;++i) {
        void *thread=dm_guest_thread_spawn(tiny_stack_worker,NULL,16,4096);
        dm_guest_thread_join(thread);
    }
    if(atomic_load(&tiny_stack_returns)!=32)die("4 KiB original SDK thread teardown failed");
    LOG("LINUX_TINY_STACK_EXIT original_sdk_threads=32 stack_bytes=4096 passed=true");
    uint32_t (*create)(void *,int,int) = required("_ZN2nn2os17CreateSystemEventEPNS0_15SystemEventTypeENS0_14EventClearModeEb");
    void (*signal_event)(void *) = required("_ZN2nn2os17SignalSystemEventEPNS0_15SystemEventTypeE");
    void (*clear_event)(void *) = required("_ZN2nn2os16ClearSystemEventEPNS0_15SystemEventTypeE");
    void (*destroy_event)(void *) = required("_ZN2nn2os18DestroySystemEventEPNS0_15SystemEventTypeE");
    int (*try_wait)(void *) = required("_ZN2nn2os18TryWaitSystemEventEPNS0_15SystemEventTypeE");
    event_wait = required("_ZN2nn2os15WaitSystemEventEPNS0_15SystemEventTypeE");
    int h0,l0,o0; thr_counts(&h0,&l0,&o0);
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        unsigned char event[256] __attribute__((aligned(16))) = {0};
        if (create(event,0,1)) die("Original SDK CreateSystemEvent failed");
        if (try_wait(event) & 1) die("Original SDK fresh event is signaled");
        atomic_store(&worker_started,0); atomic_store(&worker_done,0); atomic_store(&worker_wait_ns,0);
        uint64_t wait_count = svc_count(0x18);
        void *thread = dm_guest_thread_spawn(event_worker,event,16,64*1024);
        uint64_t deadline = now_ns() + 2000000000;
        struct timespec small = {.tv_nsec=1000000};
        while (!atomic_load(&worker_started) || svc_count(0x18) <= wait_count) {
            if (now_ns() > deadline) die("Original SDK worker did not enter event wait");
            nanosleep(&small,NULL);
        }
        struct timespec delay = {.tv_nsec=20000000}; nanosleep(&delay,NULL);
        if (atomic_load(&worker_done)) die("Original SDK wait returned before event signal");
        signal_event(event); dm_guest_thread_join(thread);
        if (!atomic_load(&worker_done) || atomic_load(&worker_wait_ns) < 10000000) die("Original SDK event wait did not complete correctly");
        if (!(try_wait(event)&1)) die("Original SDK manual event lost signal");
        clear_event(event);
        if (try_wait(event)&1) die("Original SDK event clear failed");
        destroy_event(event);
        int h1,l1,o1; thr_counts(&h1,&l1,&o1);
        if (h0!=h1 || l0!=l1 || o0!=o1) die("Original SDK thread/event resources leaked");
    }
}
