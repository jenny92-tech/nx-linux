// INPUT:  DM_LINUX_WAIT_TRACE_FILE naming an existing owned trace marker.
// OUTPUT: Kernel-clock phase records and an exit receipt with write failures.
// POS:    Default-off Linux diagnostic; initialized before guest threads.
#include "wait_trace.h"
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int dm_linux_wait_trace_enabled;
static int marker_fd = -1;
static _Atomic uint64_t sequence, failures;

static int owned_marker(const char *path) {
    static const char *const prefixes[] = {
        "/sys/kernel/debug/tracing/instances/d2r-wait-",
        "/sys/kernel/tracing/instances/d2r-wait-"
    };
    for (unsigned i = 0; i < sizeof prefixes / sizeof *prefixes; i++) {
        size_t n = strlen(prefixes[i]);
        if (strncmp(path, prefixes[i], n)) continue;
        const char *p = path + n, *start = p;
        while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
               (*p >= '0' && *p <= '9') || *p == '-' || *p == '_') p++;
        return p > start && p - start <= 80 && !strcmp(p, "/trace_marker");
    }
    return 0;
}

void dm_linux_wait_trace_emit(const char *kind, uint64_t epoch, uint64_t ticket, uint64_t value) {
    char line[256];
    uint64_t seq = atomic_fetch_add_explicit(&sequence, 1, memory_order_relaxed) + 1;
    int n = snprintf(line, sizeof line, "dm_wait seq=%llu kind=%s epoch=%llu ticket=%llu value=%llu\n",
        (unsigned long long)seq, kind, (unsigned long long)epoch,
        (unsigned long long)ticket, (unsigned long long)value);
    if (n <= 0 || n >= (int)sizeof line) { atomic_fetch_add(&failures, 1); return; }
    ssize_t wrote;
    do { wrote = write(marker_fd, line, (size_t)n); } while (wrote < 0 && errno == EINTR);
    if (wrote != n) atomic_fetch_add_explicit(&failures, 1, memory_order_relaxed);
}

static void trace_close(void) {
    if (marker_fd < 0) return;
    dm_linux_wait_trace_emit("process_exit", 0, 0, atomic_load(&failures));
    fprintf(stderr, "LINUX_WAIT_TRACE_EXIT attempted=%llu failures=%llu\n",
        (unsigned long long)atomic_load(&sequence), (unsigned long long)atomic_load(&failures));
    close(marker_fd);
}

int dm_linux_wait_trace_init(void) {
    const char *path = getenv("DM_LINUX_WAIT_TRACE_FILE");
    if (!path || !*path) return 0;
    if (!owned_marker(path)) { errno = EINVAL; return -1; }
    marker_fd = open(path, O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
    if (marker_fd < 0) return -1;
    if (atexit(trace_close)) { close(marker_fd); marker_fd = -1; errno = ENOMEM; return -1; }
    dm_linux_wait_trace_enabled = 1;
    fprintf(stderr, "LINUX_WAIT_TRACE enabled=1 observational=1 clock=kernel_trace_instance path=%s\n", path);
    DM_LINUX_WAIT_MARK("process_begin", 0, 0, 0);
    return 0;
}
