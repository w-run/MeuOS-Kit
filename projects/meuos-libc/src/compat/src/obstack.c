#include <obstack.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#define DEFAULT_CHUNK_SIZE 4096

static void
new_chunk(struct obstack *h, size_t need)
{
	size_t size = h->chunk_size;
	struct _obstack_chunk *ch;

	if (need > size)
		size = need + sizeof(struct _obstack_chunk);
	ch = malloc(size);
	if (!ch) {
		/* glibc calls the error handler; we just abort. */
		abort();
	}
	ch->prev = h->chunk;
	h->chunk = ch;
	h->object_base = ch->body;
	h->next_free = ch->body;
	h->chunk_limit = (char *)ch + size;
}

void
_obstack_maybe_grow(struct obstack *h)
{
	if (h->next_free >= h->chunk_limit) {
		size_t objsize = (size_t)(h->next_free - h->object_base);

		new_chunk(h, objsize);
		/* Move the partially-built object into the new chunk. */
		if (objsize) {
			memcpy(h->next_free, h->object_base, objsize);
			h->next_free += objsize;
		}
	}
}

int
obstack_init(struct obstack *h, size_t chunk_size)
{
	h->chunk = NULL;
	h->chunk_size = chunk_size ? chunk_size : DEFAULT_CHUNK_SIZE;
	new_chunk(h, 0);
	return 0;
}

void *
obstack_finish(struct obstack *h)
{
	void *result = h->object_base;

	h->object_base = h->next_free;
	return result;
}

size_t
obstack_object_size(struct obstack *h)
{
	return (size_t)(h->next_free - h->object_base);
}

void
obstack_blank(struct obstack *h, size_t size)
{
	h->next_free += size;
	_obstack_maybe_grow(h);
}

void
obstack_grow(struct obstack *h, const void *data, size_t size)
{
	memcpy(h->next_free, data, size);
	h->next_free += size;
	_obstack_maybe_grow(h);
}

void
obstack_grow0(struct obstack *h, const void *data, size_t size)
{
	memcpy(h->next_free, data, size);
	h->next_free += size;
	*h->next_free++ = '\0';
	_obstack_maybe_grow(h);
}

void *
obstack_copy(struct obstack *h, const void *data, size_t size)
{
	obstack_grow(h, data, size);
	return obstack_finish(h);
}

void *
obstack_copy0(struct obstack *h, const void *data, size_t size)
{
	obstack_grow0(h, data, size);
	return obstack_finish(h);
}

void *
obstack_alloc(struct obstack *h, size_t size)
{
	obstack_blank(h, size);
	return obstack_finish(h);
}

void
obstack_free(struct obstack *h, void *obj)
{
	if (!obj) {
		/* Free everything. */
		while (h->chunk) {
			struct _obstack_chunk *prev = h->chunk->prev;

			free(h->chunk);
			h->chunk = prev;
		}
		h->object_base = NULL;
		h->next_free = NULL;
		h->chunk_limit = NULL;
		return;
	}
	/* Pop chunks until obj is within the current chunk. */
	while (h->chunk && (obj < (void *)h->chunk
	     || obj >= (void *)h->chunk_limit)) {
		struct _obstack_chunk *prev = h->chunk->prev;

		free(h->chunk);
		h->chunk = prev;
	}
	if (!h->chunk) {
		h->object_base = NULL;
		h->next_free = NULL;
		h->chunk_limit = NULL;
		return;
	}
	h->object_base = (char *)obj;
	h->next_free = (char *)obj;
}

int
obstack_printf(struct obstack *h, const char *format, ...)
{
	va_list ap;
	char buf[512];
	int len;

	va_start(ap, format);
	len = vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	if (len < 0)
		return -1;
	obstack_grow(h, buf, (size_t)len);
	return len;
}
