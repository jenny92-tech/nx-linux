// INPUT: Physical memory classes and extreme counter values.
// OUTPUT: A bounded system reserve for small handhelds, without device access.
// POS: Host arithmetic regression; allocation rejection is covered by memory_guard.c.
#include "../memory_guard.h"
#include <assert.h>
int main(void) {
    const uint64_t mib=1024ull*1024;
    assert(dm_linux_memory_reserve_for_total(0)==128*mib);
    assert(dm_linux_memory_reserve_for_total(512*mib)==128*mib);
    assert(dm_linux_memory_reserve_for_total(1024*mib)==128*mib);
    assert(dm_linux_memory_reserve_for_total(2048*mib)==256*mib);
    assert(dm_linux_memory_reserve_for_total(4096*mib)==512*mib);
    assert(dm_linux_memory_reserve_for_total(UINT64_MAX)==512*mib);
    return 0;
}
