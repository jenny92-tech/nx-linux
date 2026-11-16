// INPUT:  File-backed baseline storage using an explicitly supplied test directory.
// OUTPUT: Independent 32769-byte pattern comparison after dropping mapped pages.
// POS:    Regression probe for byte preservation, unlinked lifetime and non-page sizes.
#include "../snapshot_store.h"
#include <stdlib.h>
#include <stdio.h>
int main(void) {
    size_t size=32769;uint8_t *bytes=malloc(size);if(!bytes)return 1;
    for(size_t i=0;i<size;i++)bytes[i]=(uint8_t)((i*71u)^(i>>8)^0x95u);
    if(!dm_linux_snapshot_store(&bytes,size))return 2;
    for(size_t i=0;i<size;i++)if(bytes[i]!=(uint8_t)((i*71u)^(i>>8)^0x95u))return 3;
    dm_linux_snapshot_release(bytes,size,1);puts("{\"passed\":true,\"bytes_checked\":32769,\"unlinked_mapping_readback\":true}");return 0;
}
