/* stream.c — .msys streaming reader
 *
 * Sequential read of all (name, data) pairs from a .msys archive.
 * For archives with MSYS_F_STREAMING flag, data blocks are ordered
 * in the same order as index entries.
 *
 * The streaming reader loads the index on open to get entry names/sizes,
 * then reads data blocks sequentially from the data area.
 */

#include "mt/msys.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- helpers ---- */

static uint16_t r16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint64_t r48(const uint8_t *p) { return (uint64_t)r16(p) | ((uint64_t)r16(p+2)<<16) | ((uint64_t)r16(p+4)<<32); }

/* Entry info loaded from index */
struct stream_entry {
	const char *name;
	size_t      nlen;
	size_t      dsize;
	uint64_t    data_off;  /* offset of data within mmap */
};

struct msys_stream {
	void              *base;
	size_t             size;
	struct stream_entry *entries;
	size_t             count;   /* total entries */
	size_t             pos;     /* current entry index */
};

struct msys_stream *
msys_stream_open(const char *path)
{
	if (!path) { errno = EINVAL; return NULL; }

	struct msys *m = msys_open(path);
	if (!m) return NULL;

	uint32_t cnt = msys_count(m);
	if (cnt == 0) { msys_close(m); errno = ENOENT; return NULL; }

	struct stream_entry *entries = calloc(cnt, sizeof(struct stream_entry));
	if (!entries) { msys_close(m); return NULL; }

	size_t valid = 0;
	for (uint32_t i = 0; i < cnt; i++) {
		const char *name; size_t nlen, dsize;
		if (msys_enumerate(m, i, &name, &nlen, &dsize) < 0)
			continue;

		/* Skip @mt metadata entries */
		if (nlen > 0 && name[0] == '@') continue;

		entries[valid].name = name;
		entries[valid].nlen = nlen;
		entries[valid].dsize = dsize;

		/* Determine data offset from the index entry */
		int v2 = (msys_format_version(m) == MSYS_FORMAT_V2);
		unsigned char *ep = m->entries[i];
		entries[valid].data_off = v2 ? r48(ep + 4) : r48(ep + 4);
		valid++;
	}

	struct msys_stream *s = calloc(1, sizeof(*s));
	if (!s) { free(entries); msys_close(m); return NULL; }

	/* Transfer ownership: keep the mmap'd base for data access */
	s->base    = m->base;
	s->size    = m->size;
	s->entries = entries;
	s->count   = valid;
	s->pos     = 0;

	/* Free the msys handle but keep the mmap alive */
	free(m->entries);
	free(m->chunks);
	free(m);

	return s;
}

int
msys_stream_next(struct msys_stream *s,
                 const char **name, size_t *nlen,
                 const void **data, size_t *dsize)
{
	if (!s || !name || !nlen || !data || !dsize) { errno = EINVAL; return -1; }
	if (s->pos >= s->count) return 0;

	struct stream_entry *e = &s->entries[s->pos];

	*name  = e->name;
	*nlen  = e->nlen;
	*data  = (const uint8_t *)s->base + e->data_off;
	*dsize = e->dsize;
	s->pos++;
	return 1;
}

void
msys_stream_close(struct msys_stream *s)
{
	if (!s) return;
	munmap(s->base, s->size);
	free(s->entries);
	free(s);
}
