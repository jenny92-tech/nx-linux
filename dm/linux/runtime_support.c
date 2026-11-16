// INPUT:  Linux signals/getrandom and shared Horizon kernel/service implementations.
// OUTPUT:  Bring-up diagnostics, entropy, and explicit unsupported-private-call handling.
// POS:    Supply Linux host plumbing while original guest initialization executes.
#include "../host/dm.h"
#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
#include <sys/random.h>
#include <sys/uio.h>
#include <unistd.h>

#ifndef DM_LINUX_GFX
static pthread_once_t stack_once = PTHREAD_ONCE_INIT;
static pthread_key_t stack_owner;
static void stack_release(void *memory) {
    stack_t disabled = {.ss_flags = SS_DISABLE};
    sigaltstack(&disabled, NULL);
    free(memory);
}
static void stack_key(void) {
    if (pthread_key_create(&stack_owner, stack_release)) die("Linux signal-stack key");
}
void crash_thread_init(void) {
    pthread_once(&stack_once, stack_key);
    if (pthread_getspecific(stack_owner)) return;
    void *memory = malloc(64 * 1024);
    stack_t stack = {.ss_sp = memory, .ss_size = 64 * 1024};
    if (!memory || sigaltstack(&stack, NULL) || pthread_setspecific(stack_owner, memory))
        die("Linux signal-stack allocation");
}
#endif
void arc4random_buf(void *buffer, size_t size) {
    unsigned char *out = buffer;
    while (size) {
        ssize_t count = getrandom(out, size, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) die("Linux getrandom");
        out += count; size -= (size_t)count;
    }
}
uint32_t arc4random(void) { uint32_t value; arc4random_buf(&value, sizeof value); return value; }
#ifndef DM_LINUX_GFX
static _Atomic uint64_t svc_calls[256], profile_ns[PROF_N], profile_bytes[PROF_N];
void hlecount_svc(int number) { if (number >= 0 && number < 256) atomic_fetch_add(&svc_calls[number],1); }
uint64_t dm_linux_svc_count(unsigned number) { return number < 256 ? atomic_load(&svc_calls[number]) : 0; }
void dm_prof_add(int category, uint64_t ns, uint64_t bytes) {
    if (category < 0 || category >= PROF_N) die("Linux profile category");
    atomic_fetch_add(&profile_ns[category],ns);
    atomic_fetch_add(&profile_bytes[category],bytes);
}
uint64_t svc_host_call(int number, uint64_t *registers) { (void)number; (void)registers; return 0; }
void dm_backtrace(uint64_t frame) {
    for (unsigned depth = 0; frame && depth < 24; ++depth) {
        uint64_t pair[2];
        struct iovec local = {.iov_base = pair, .iov_len = sizeof pair};
        struct iovec remote = {.iov_base = (void *)(uintptr_t)frame, .iov_len = sizeof pair};
        if (process_vm_readv(getpid(), &local, 1, &remote, 1, 0) != sizeof pair) break;
        module_t *module = module_of((void *)(uintptr_t)pair[1]);
        if (module) LOG("Linux backtrace #%u %s+%#llx",depth,module->name,pair[1]-(uint64_t)(uintptr_t)module->base);
        else LOG("Linux backtrace #%u host=%#llx",depth,pair[1]);
        if (pair[0] <= frame || pair[0] - frame > 1024 * 1024) break;
        frame = pair[0];
    }
}
void crash_watch_frame(void) {}
void uimsg_subscribe(void (*callback)(const char *, const char *, const char *)) { (void)callback; }
#endif
int dm_ingame_now(void) { return 0; }
uint64_t dm_server_tick(void) { return UINT64_MAX; }
/* This CPU-only executable never protects GPU-backed guest pages. The graphics
 * executable must use the recorder's write-preparation hooks instead. */
#ifndef DM_LINUX_GFX
void dm_guest_write_prepare(void *address, size_t length) { (void)address; (void)length; }
#endif
/* No custom settings UI has been applied; retain the original game's choice. */
int dm_custom_lang_explicit(void) { return -1; }
