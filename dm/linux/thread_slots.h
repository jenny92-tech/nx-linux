// INPUT:  pthread keys and compiler-owned ELF TLS.
// OUTPUT:  Linux guest-context slots and their measured thread-pointer offset.
// POS:    Keep guest TLS separate from glibc's thread-control block.
#pragma once
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
uintptr_t *dm_linux_slots(void);
void dm_linux_slots_sync(void);
void dm_linux_slot_set(pthread_key_t key, void *value);
uint64_t dm_linux_slot_offset(unsigned index);
uint64_t dm_linux_tls_offset(void);
void dm_linux_slots_detach(void);
size_t strlcpy(char *dst, const char *src, size_t size);
uint32_t arc4random(void);
void arc4random_buf(void *buffer, size_t size);

int dm_linux_thread_has_guest_context(void);
