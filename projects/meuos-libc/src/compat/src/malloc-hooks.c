#include <stddef.h>

/* glibc-style malloc debug hooks.  Weak, NULL defaults so that a program
 * may install its own interceptor; the core malloc/free/realloc ignore
 * them.  Provided as empty weak symbols per AGENTS.md §2.2 item 5. */
void *(*volatile __malloc_hook)(size_t, const void *) __attribute__((weak));
void (*volatile __free_hook)(void *, const void *) __attribute__((weak));
void *(*volatile __realloc_hook)(void *, size_t, const void *) __attribute__((weak));
void *(*volatile __memalign_hook)(size_t, size_t, const void *) __attribute__((weak));
