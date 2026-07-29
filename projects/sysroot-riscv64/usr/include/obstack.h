#ifndef MEUOS_OBSTACK_H
#define MEUOS_OBSTACK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal obstack: a growable LIFO object allocator.  Each obstack grows
 * objects within a linked list of chunks; obstack_finish() freezes the
 * growing object and returns a pointer to it, obstack_free() pops back to
 * a given point (or the whole stack). */

struct _obstack_chunk {
	struct _obstack_chunk *prev;
	char body[];
};

struct obstack {
	struct _obstack_chunk *chunk;
	char *object_base;
	char *next_free;
	char *chunk_limit;
	size_t chunk_size;
};

#define obstack_1grow(h, c) do { *(h)->next_free++ = (char)(c); \
	_obstack_maybe_grow(h); } while (0)

int obstack_init(struct obstack *, size_t);
void *obstack_alloc(struct obstack *, size_t);
void *obstack_copy(struct obstack *, const void *, size_t);
void *obstack_copy0(struct obstack *, const void *, size_t);
void obstack_blank(struct obstack *, size_t);
void obstack_grow(struct obstack *, const void *, size_t);
void obstack_grow0(struct obstack *, const void *, size_t);
void *obstack_finish(struct obstack *);
size_t obstack_object_size(struct obstack *);
void obstack_free(struct obstack *, void *);
int obstack_printf(struct obstack *, const char *, ...);

/* Internal: grow the chunk when next_free would exceed chunk_limit. */
void _obstack_maybe_grow(struct obstack *);

#ifdef __cplusplus
}
#endif

#endif
