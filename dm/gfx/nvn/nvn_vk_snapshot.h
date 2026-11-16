// INPUT: Owned immutable texture comparison bytes and their original length.
// OUTPUT: Lossless compact storage and an exact, temporary comparison view.
// POS: CPU baselines only; GPU formats, uploaded pixels and block ownership stay intact.
#ifndef NVN_VK_SNAPSHOT_H
#define NVN_VK_SNAPSHOT_H
#include <stdint.h>
#include <stdlib.h>
#include <zlib.h>

/* Keep small or incompressible snapshots as-is. Compression failure preserves
 * the caller's original allocation, including its ownership and exact bytes. */
static size_t nvn_vk_snapshot_pack(uint8_t **bytes,size_t size) {
    if(!bytes||!*bytes||size<1024)return size;
    uLongf packed_size=size-size/8;
    uint8_t *packed=malloc(packed_size);if(!packed)return size;
    if(compress2(packed,&packed_size,*bytes,size,Z_BEST_SPEED)!=Z_OK){free(packed);return size;}
    uint8_t *exact=realloc(packed,packed_size);
    if(!exact){free(packed);return size;}
    free(*bytes);*bytes=exact;return packed_size;
}

/* The scratch belongs to the caller and must be freed after comparison. No
 * mutable reference to a compact baseline escapes; ordinary draws never open it. */
static const uint8_t *nvn_vk_snapshot_open(const uint8_t *bytes,size_t stored_size,size_t raw_size,uint8_t **scratch) {
    *scratch=NULL;
    if(!bytes||!stored_size||stored_size>raw_size)return NULL;
    if(stored_size==raw_size)return bytes;
    uint8_t *raw=malloc(raw_size);if(!raw)return NULL;
    uLongf decoded=raw_size;
    if(uncompress(raw,&decoded,bytes,stored_size)!=Z_OK||decoded!=raw_size){free(raw);return NULL;}
    *scratch=raw;return raw;
}
#endif
