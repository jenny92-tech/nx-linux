// INPUT:  The real Linux memory guard, without allocating guest/GPU memory.
// OUTPUT: Exit 75 and a receipt for an intentionally impossible request.
// POS:    Regression probe for rejecting a request before any allocation.
#include "../memory_guard.h"
int main(void) {
    dm_linux_memory_guard_init();
    if(!dm_linux_memory_guard_optional(0)||dm_linux_memory_guard_optional(UINT64_MAX))return 2;
    dm_linux_memory_guard_allocation(UINT64_MAX,123,4);
    return 1;
}
