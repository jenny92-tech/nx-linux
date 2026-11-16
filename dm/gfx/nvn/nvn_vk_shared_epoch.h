// INPUT: Recorder epoch, shared-pool ownership and actual GPU completion epoch.
// OUTPUT: Only the matching submission may release pending CPU ownership.
// POS: Pure ownership state machine; callers still perform DMA sync and wait fences.
#pragma once
#include <stdint.h>
typedef struct {uint64_t epoch;unsigned owned,pending;uint64_t completed_epoch;} NvnVkSharedEpoch;
static inline int nvn_vk_shared_epoch_acquire(NvnVkSharedEpoch *s,uint64_t epoch) {
    if(!epoch||s->pending||(s->owned&&s->epoch!=epoch))return 0;
    s->epoch=epoch;s->owned=1;return 1;
}
static inline int nvn_vk_shared_epoch_release(NvnVkSharedEpoch *s,uint64_t epoch) {
    if(!s->owned||s->pending||!epoch||s->epoch!=epoch)return 0;
    s->owned=0;s->pending=1;return 1;
}
static inline int nvn_vk_shared_epoch_pending(const NvnVkSharedEpoch *s,uint64_t epoch) {
    return epoch&&s->pending&&!s->owned&&s->epoch==epoch;
}
static inline int nvn_vk_shared_epoch_complete(NvnVkSharedEpoch *s,uint64_t epoch) {
    if(!nvn_vk_shared_epoch_pending(s,epoch))return 0;
    s->completed_epoch=epoch;s->pending=0;s->epoch=0;return 1;
}
/* The importer maps the same DMA-BUF/offset at both virtual addresses. Its
 * uncached CPU ownership must have returned after this exact GPU fence. */
static inline int nvn_vk_shared_alias_writeback_ready(const NvnVkSharedEpoch *s,
        uint64_t epoch,int imported,int uncached){
    return s&&imported&&uncached&&epoch&&s->completed_epoch==epoch&&
        !s->epoch&&!s->owned&&!s->pending;
}
