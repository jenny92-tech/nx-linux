// INPUT:  Explicit diagnostic opt-in and immutable submission identifiers.
// OUTPUT: Bounded phase markers in an owned Linux trace instance.
// POS:    Observe waits without changing fences, publication or scheduling.
#ifndef DM_LINUX_WAIT_TRACE_H
#define DM_LINUX_WAIT_TRACE_H
#include <stdint.h>
#ifdef DM_LINUX_HOST
extern int dm_linux_wait_trace_enabled;
int dm_linux_wait_trace_init(void);
void dm_linux_wait_trace_emit(const char *kind, uint64_t epoch, uint64_t ticket, uint64_t value);
#define DM_LINUX_WAIT_MARK(kind, epoch, ticket, value) do { \
    if (dm_linux_wait_trace_enabled) dm_linux_wait_trace_emit(kind, epoch, ticket, value); \
} while (0)
#else
#define DM_LINUX_WAIT_MARK(kind, epoch, ticket, value) ((void)0)
#endif
#endif
