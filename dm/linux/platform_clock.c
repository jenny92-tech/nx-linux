// INPUT:  POSIX monotonic/thread clocks and NVN workload/wait notifications.
// OUTPUT:  Native scheduling, absolute pacing and measured frame CPU/wait times.
// POS:    Implement the Vulkan recorder clock ABI without Android hint services.
#include "../host/dm.h"
#include "../android/nvn_platform_clock.h"
#include <errno.h>
#include <time.h>

static uint64_t clock_ns(clockid_t id) {
    struct timespec time;
    if (clock_gettime(id, &time)) die("Linux clock_gettime: %d", errno);
    return (uint64_t)time.tv_sec * 1000000000 + (uint64_t)time.tv_nsec;
}
uint64_t dm_android_nvn_clock_ns(void) { return clock_ns(CLOCK_MONOTONIC); }
uint64_t dm_android_nvn_thread_cpu_ns(void) { return clock_ns(CLOCK_THREAD_CPUTIME_ID); }
void dm_android_nvn_sleep_until(uint64_t deadline) {
    struct timespec time = {(time_t)(deadline / 1000000000), (long)(deadline % 1000000000)};
    int error;
    do { error = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &time, NULL); } while (error == EINTR);
    if (error) die("Linux absolute sleep: %d", error);
}
void dm_android_nvn_schedule_thread(const char *role) {
    char name[16]; snprintf(name, sizeof name, "nvn-%.11s", role);
    int error = pthread_setname_np(pthread_self(), name);
    if (error) LOG("Linux thread name error=%d", error);
}
static _Thread_local struct {
    uint64_t start, cpu, waiting, wait_start;
    unsigned wait_depth;
} work;
void dm_android_nvn_work_begin(void) {
    work.start = dm_android_nvn_clock_ns(); work.cpu = dm_android_nvn_thread_cpu_ns();
    work.waiting = 0; work.wait_depth = 0;
}
void dm_android_nvn_work_check_thread(void) { if (!work.start) dm_android_nvn_work_begin(); }
void dm_android_nvn_work_end(uint64_t frame) {
    if (work.start && getenv("DM_WORK_STATS"))
        LOG("LINUX_NV_WORK frame=%llu wall_ns=%llu cpu_ns=%llu wait_ns=%llu",
            (unsigned long long)frame, (unsigned long long)(dm_android_nvn_clock_ns() - work.start),
            (unsigned long long)(dm_android_nvn_thread_cpu_ns() - work.cpu), (unsigned long long)work.waiting);
    work.start = 0;
}
void dm_android_nvn_work_reset(int foreground) { (void)foreground; memset(&work, 0, sizeof work); }
uint64_t dm_android_nvn_work_wait_begin(enum dm_android_nvn_wait_kind kind) {
    if (kind < 0 || kind >= DM_NVN_WAIT_KIND_COUNT) die("Linux NVN wait kind");
    if (!work.wait_depth++) work.wait_start = dm_android_nvn_clock_ns();
    return work.wait_depth;
}
void dm_android_nvn_work_wait_end(uint64_t token) {
    if (!token || token != work.wait_depth) die("Linux NVN wait nesting");
    if (!--work.wait_depth) work.waiting += dm_android_nvn_clock_ns() - work.wait_start;
}
