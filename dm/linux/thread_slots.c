// INPUT:  Host pthread keys and an initial-exec TLS pointer owned by this executable.
// OUTPUT:  dm_linux_slots(), synchronization, offsets and context teardown.
// POS:    Provide seek-independent guest thread state without borrowing glibc fields.
#include "../host/dm.h"

static __thread uintptr_t *context __attribute__((tls_model("initial-exec")));
static pthread_key_t owner;
static pthread_once_t once = PTHREAD_ONCE_INIT;

static pthread_key_t slot_key(unsigned index) {
    pthread_key_t keys[] = {g_key18, g_keytls, g_keytlr, g_keygate,
        g_keyres[0], g_keyres[1], g_keyaspect, g_keyaspectui};
    if (index >= 8) die("Linux guest slot index %u", index);
    return keys[index];
}
static void release(void *value) { context = NULL; free(value); }
static void initialize(void) {
    if (pthread_key_create(&owner, release)) die("Linux context destructor key");
}
uintptr_t *dm_linux_slots(void) {
    if (!context) {
        pthread_once(&once, initialize);
        uintptr_t *value = calloc(9, sizeof *value);
        if (!value || pthread_setspecific(owner, value)) die("Linux guest context allocation");
        context = value;
    }
    return context;
}
uint64_t dm_linux_tls_offset(void) {
    return (uintptr_t)&context - (uintptr_t)__builtin_thread_pointer();
}
uint64_t dm_linux_slot_offset(unsigned index) {
    if (index >= 8) die("Linux guest slot offset %u", index);
    return (index + 1) * sizeof(uintptr_t);
}
void dm_linux_slots_sync(void) {
    uintptr_t *slots = dm_linux_slots();
    for (unsigned i = 0; i < 8; ++i) slots[i + 1] = (uintptr_t)pthread_getspecific(slot_key(i));
}
void dm_linux_slot_set(pthread_key_t key, void *value) {
    if (pthread_setspecific(key, value)) die("Linux guest slot write");
    uintptr_t *slots = dm_linux_slots();
    for (unsigned i = 0; i < 8; ++i)
        if (slot_key(i) == key) { slots[i + 1] = (uintptr_t)value; return; }
    die("Linux unknown guest key");
}
void dm_linux_slots_detach(void) {
    if (context) {
        uintptr_t *value = context;
        pthread_setspecific(owner, NULL);
        release(value);
    }
}
size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t n = strlen(src);
    if (size) { size_t copied = n < size - 1 ? n : size - 1; memcpy(dst, src, copied); dst[copied] = 0; }
    return n;
}

int dm_linux_thread_has_guest_context(void) { return context != NULL; }
