// INPUT:  Owned CPU baseline bytes and an optional private cache directory.
// OUTPUT: Identical owned bytes, with bounded optional RAM and file-backed fallback.
// POS:    Preserve lossless baselines; avoid synchronous small-file I/O within an explicit budget.
#pragma once
#include <stddef.h>
#include <stdint.h>
int dm_linux_snapshot_store(uint8_t **bytes,size_t size);
void dm_linux_snapshot_release(uint8_t *bytes,size_t size,int mapped);
typedef struct {
    uint64_t limit_bytes,resident_bytes,peak_bytes,ram_stores,file_stores,resident_entries;
} DmLinuxSnapshotStats;
void dm_linux_snapshot_stats(DmLinuxSnapshotStats *out);
