// INPUT:  Actual Linux MemAvailable and backend allocation requests.
// OUTPUT: Early process exit before exhausting the handheld's shared RAM.
// POS:    Bring-up containment; no global governor, swap or OOM policy changes.
#pragma once
#include <stdint.h>
/* Keep one eighth for the system, bounded to the existing 512 MiB reserve. */
static inline uint64_t dm_linux_memory_reserve_for_total(uint64_t total) {
    const uint64_t minimum=128ull*1024*1024,maximum=512ull*1024*1024;
    uint64_t value=total/8;
    return value<minimum?minimum:value>maximum?maximum:value;
}
void dm_linux_memory_guard_init(void);
void dm_linux_memory_guard_poll(void);
void dm_linux_memory_guard_allocation(uint64_t bytes,uint64_t live,unsigned kind);
/* Optional diagnostics must leave extra headroom and may be omitted. */
int dm_linux_memory_guard_optional(uint64_t bytes);
