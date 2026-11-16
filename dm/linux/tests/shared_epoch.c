// INPUT: Interleaved record/complete epochs for two independent shared pools.
// OUTPUT: Stale/present completions cannot release another submission's pool.
// POS: Host ownership-state regression; no GPU or DMA ioctl is simulated as success.
#include "nvn_vk_shared_epoch.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    NvnVkSharedEpoch p={0},q={0};
    assert(!nvn_vk_shared_epoch_acquire(&p,0));
    assert(nvn_vk_shared_epoch_acquire(&p,10));
    assert(nvn_vk_shared_epoch_acquire(&p,10));
    assert(!nvn_vk_shared_epoch_acquire(&p,11));
    assert(!nvn_vk_shared_epoch_release(&p,11));
    assert(!nvn_vk_shared_epoch_complete(&p,10));
    assert(nvn_vk_shared_epoch_release(&p,10));
    assert(!nvn_vk_shared_epoch_acquire(&p,11));
    assert(!nvn_vk_shared_epoch_complete(&p,9));
    assert(p.pending&&p.epoch==10&&!p.owned);
    assert(nvn_vk_shared_epoch_acquire(&q,12));
    assert(nvn_vk_shared_epoch_release(&q,12));
    assert(nvn_vk_shared_epoch_complete(&p,10));
    assert(nvn_vk_shared_alias_writeback_ready(&p,10,1,1));
    assert(!nvn_vk_shared_alias_writeback_ready(&p,10,0,1));
    assert(!nvn_vk_shared_alias_writeback_ready(&p,10,1,0));
    assert(!nvn_vk_shared_alias_writeback_ready(&p,11,1,1));
    assert(!nvn_vk_shared_epoch_complete(&q,10));
    assert(q.pending&&q.epoch==12);
    assert(nvn_vk_shared_epoch_acquire(&p,13));
    assert(!nvn_vk_shared_alias_writeback_ready(&p,10,1,1));
    assert(nvn_vk_shared_epoch_release(&p,13));
    assert(!nvn_vk_shared_alias_writeback_ready(&p,13,1,1));
    /* An older present completes while a newer game submission owns this pool. */
    assert(!nvn_vk_shared_epoch_complete(&p,11));
    assert(!nvn_vk_shared_epoch_complete(&p,10));
    assert(nvn_vk_shared_epoch_pending(&p,13));
    assert(nvn_vk_shared_epoch_complete(&q,12));
    assert(nvn_vk_shared_epoch_pending(&p,13));
    assert(nvn_vk_shared_epoch_complete(&p,13));
    assert(nvn_vk_shared_alias_writeback_ready(&p,13,1,1));
    assert(!nvn_vk_shared_epoch_complete(&p,13));
    assert(!p.epoch&&!p.pending&&!p.owned&&!q.epoch&&!q.pending&&!q.owned);
    puts("shared epoch: stale completion, pending reuse, recorder identity, independent pools passed");
}
