#include "mt/msys.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- Minimal zlib struct for dynamic decompression ---- */
typedef struct {
	unsigned char *next_in;  unsigned int avail_in;  unsigned long total_in;
	unsigned char *next_out; unsigned int avail_out; unsigned long total_out;
	const char *msg; void *state;
	void *(*zalloc)(void *, unsigned int, unsigned int);
	void  (*zfree)(void *, void *);
	void *opaque;
	int data_type; unsigned long adler; unsigned long reserved;
} z_stream_min;
#define Z_OK            0
#define Z_STREAM_END    1
#define Z_FINISH 4

/* ---- helpers for uint48 (6-byte LE) ---- */

static uint64_t read48(const uint8_t *buf)
{
	return (uint64_t)buf[0]
	     | ((uint64_t)buf[1] << 8)
	     | ((uint64_t)buf[2] << 16)
	     | ((uint64_t)buf[3] << 24)
	     | ((uint64_t)buf[4] << 32)
	     | ((uint64_t)buf[5] << 40);
}

static uint32_t read32(const uint8_t *buf)
{
	return (uint32_t)buf[0]
	     | ((uint32_t)buf[1] << 8)
	     | ((uint32_t)buf[2] << 16)
	     | ((uint32_t)buf[3] << 24);
}

static uint16_t read16(const uint8_t *buf)
{
	return (uint16_t)buf[0]
	     | ((uint16_t)buf[1] << 8);
}

/* ---- FNV-1a 32-bit ---- */

uint32_t msys_fnv1a(const unsigned char *name, size_t len)
{
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < len; i++) {
		h ^= name[i];
		h *= 16777619u;
	}
	return h;
}

/* ---- open / close ---- */

struct msys *msys_open(const char *path)
{
	struct stat st;
	int fd;
	void *base;
	struct msys *m;

	if (!path) { errno = EINVAL; return NULL; }

	fd = open(path, O_RDONLY);
	if (fd < 0) return NULL;

	if (fstat(fd, &st) < 0) { close(fd); return NULL; }
	if (st.st_size < (off_t)sizeof(struct msys_header)) {
		close(fd); errno = EINVAL; return NULL;
	}

	base = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (base == MAP_FAILED) return NULL;

	/* Validate magic */
	if (memcmp(base, MSYS_MAGIC, MSYS_MAGIC_LEN) != 0) {
		munmap(base, (size_t)st.st_size);
		errno = EINVAL;
		return NULL;
	}

	m = calloc(1, sizeof(*m));
	if (!m) { munmap(base, (size_t)st.st_size); return NULL; }

	m->base  = base;
	m->size  = (size_t)st.st_size;
	m->hdr   = (struct msys_header *)base;

	uint64_t index_off = m->hdr->index_offset;

	/* Validate index offset fits within the mapped region.
	 * We don't know the exact index size yet (variable-length names),
	 * but we validate at search time. At minimum ensure the header
	 * doesn't claim a nonsense offset. */
	if (index_off < sizeof(struct msys_header) ||
	    index_off >= m->size) {
		munmap(base, m->size); free(m);
		errno = EINVAL;
		return NULL;
	}

	m->index = (struct msys_index_entry *)((unsigned char *)base + index_off);

	/* Build per-entry pointer array (entries are variable-length) */
	uint32_t count = m->hdr->index_count;
	if (count > 0) {
		m->entries = calloc(count, sizeof(unsigned char *));
		if (!m->entries) {
			munmap(base, m->size); free(m);
			return NULL;
		}
		unsigned char *p = (unsigned char *)m->index;
		uint64_t avail = m->size - index_off;
		for (uint32_t i = 0; i < count; i++) {
			m->entries[i] = p;
			/* entry is 16 + name_len bytes; validate bounds */
			if (avail < 16) {
				munmap(base, m->size); free(m->entries); free(m);
				errno = EINVAL;
				return NULL;
			}
			uint16_t nlen = (uint16_t)p[14] | ((uint16_t)p[15] << 8);
			uint64_t ent = 16 + (uint64_t)nlen;
			if (avail < ent) {
				munmap(base, m->size); free(m->entries); free(m);
				errno = EINVAL;
				return NULL;
			}
			p += ent;
			avail -= ent;
		}
	}

	return m;
}

void msys_close(struct msys *m)
{
	if (!m) return;
	free(m->entries);
	munmap(m->base, m->size);
	free(m);
}

/* ---- search (binary search by name_hash, verify name string) ---- */

const void *msys_search(struct msys *m, const char *name, size_t *size)
{
	size_t name_len;
	uint32_t target_hash;
	uint32_t lo, hi;

	if (!m || !name) { errno = EINVAL; return NULL; }

	name_len = strlen(name);
	target_hash = msys_fnv1a((const unsigned char *)name, name_len);

	lo = 0;
	hi = m->hdr->index_count;

	while (lo < hi) {
		uint32_t mid = lo + (hi - lo) / 2;
		unsigned char *entry = m->entries[mid];
		uint32_t entry_hash = read32(entry);

		if (entry_hash < target_hash) {
			lo = mid + 1;
		} else if (entry_hash > target_hash) {
			hi = mid;
		} else {
			/* Hash match — verify name to handle collisions.
			 * Scan backward to first matching-hash entry, then forward. */
			uint32_t scan = mid;
			while (scan > lo) {
				unsigned char *p = m->entries[scan - 1];
				if (read32(p) != target_hash) break;
				scan--;
			}
			/* scan now points to first entry with matching hash */
			while (scan < m->hdr->index_count) {
				unsigned char *p = m->entries[scan];
				if (read32(p) != target_hash) break;

				uint16_t ename_len = read16(p + 14);
				const char *ename = (const char *)(p + 16);
				if (ename_len == name_len &&
				    memcmp(ename, name, name_len) == 0) {
					/* Found! */
					uint64_t off = read48(p + 4);
					uint32_t dsize = read32(p + 10);
					if (size) *size = dsize;
					if (off + dsize > m->size) {
						/* Corrupt: data extends past file */
						errno = EIO;
						return NULL;
					}
					return (const unsigned char *)m->base + off;
				}
				scan++;
			}
			/* Hash collision but no name match */
			errno = ENOMSG;
			return NULL;
		}
	}

	/* Not found at all */
	errno = ENOENT;
	return NULL;
}

/* ---- read ---- */

int msys_read(struct msys *m, const char *name, void *buf, size_t buflen)
{
	size_t dsize;
	const void *data;

	data = msys_search(m, name, &dsize);
	if (!data) return -1;

	if (buflen > dsize)
		buflen = dsize;
	memcpy(buf, data, buflen);
	return (int)buflen;
}

/* ---- VFS: fopen a file within the archive ---- */

/* Decompress data if the archive has MSYS_F_ZLIB flag set.
 * Returns allocated buffer (caller must free) on success, NULL on error.
 * On success, *out_size receives decompressed size. */
static void *
decompress(struct msys *m, const void *compressed, size_t csize, size_t *out_size)
{
	(void)m; /* unused - flag is checked by callers */
	static void *zlib_handle;
	static int (*zlib_inflateInit)(void *, const char *, int);
	static int (*zlib_inflate)(void *, int);
	static int (*zlib_inflateEnd)(void *);
	static int zlib_loaded;

	if (!zlib_loaded) {
		zlib_loaded = 1;
		zlib_handle = dlopen("libz.so.1", RTLD_LAZY | RTLD_LOCAL);
		if (!zlib_handle) zlib_handle = dlopen("libz.so", RTLD_LAZY | RTLD_LOCAL);
		if (zlib_handle) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
			zlib_inflateInit = (int (*)(void *, const char *, int))dlsym(zlib_handle, "inflateInit_");
			zlib_inflate     = (int (*)(void *, int))dlsym(zlib_handle, "inflate");
			zlib_inflateEnd  = (int (*)(void *))dlsym(zlib_handle, "inflateEnd");
#pragma GCC diagnostic pop
		}
	}

	if (!zlib_handle || !zlib_inflate || !zlib_inflateInit)
		return NULL; /* No decompression available */

	unsigned long guess = csize * 4 + 1024; /* conservative estimate */
	void *decomp = malloc(guess);
	if (!decomp) return NULL;

	z_stream_min strm;
	memset(&strm, 0, sizeof(strm));
	strm.next_in = (unsigned char *)compressed;
	strm.avail_in = (unsigned int)csize;
	strm.next_out = (unsigned char *)decomp;
	strm.avail_out = (unsigned int)guess;

	if ((*zlib_inflateInit)(&strm, "1.", (int)sizeof(z_stream_min)) != Z_OK) {
		free(decomp); return NULL;
	}

	/* Inflate with retry for buffer growth */
	int ret;
	while (1) {
		ret = (*zlib_inflate)(&strm, Z_FINISH);
		if (ret == Z_STREAM_END) break;
		if (ret != Z_OK && strm.avail_out != 0) {
			/* Error, not just need more space */
			(*zlib_inflateEnd)(&strm);
			free(decomp); return NULL;
		}
		/* Need more space - double the buffer */
		size_t consumed = (size_t)(strm.next_out - (unsigned char *)decomp);
		guess *= 2;
		void *newbuf = realloc(decomp, guess);
		if (!newbuf) { free(decomp); (*zlib_inflateEnd)(&strm); return NULL; }
		decomp = newbuf;
		strm.next_out = (unsigned char *)decomp + consumed;
		strm.avail_out = (unsigned int)(guess - consumed);
	}
	(*zlib_inflateEnd)(&strm);

	*out_size = strm.total_out;
	return decomp;
}

FILE *msys_fopen(struct msys *m, const char *path, const char *mode)
{
	size_t dsize;
	const void *data;

	if (!m || !path) { errno = EINVAL; return NULL; }
	data = msys_search(m, path, &dsize);
	if (!data) return NULL;

	if (m->hdr->flags & MSYS_F_ZLIB) {
		size_t usize;
		void *decomp = decompress(m, data, dsize, &usize);
		if (!decomp) return NULL;
		/* fmemopen does NOT adopt the buffer; we'd leak.
		 * Use tmpfile + write instead, or use funopen.
		 * Simplest: msys_fopen on compressed data is not supported yet.
		 * Return NULL; callers should use msys_load for compressed archives. */
		free(decomp);
		errno = ENOTSUP;
		return NULL;
	}

	/* fmemopen needs a non-const buffer; msys_search returns const from mmap.
	 * For "r" mode, casting const away is safe since fmemopen won't write. */
	return fmemopen((void *)data, dsize, mode);
}

/* ---- VFS: load file content into malloc'd memory ---- */

int msys_load(struct msys *m, const char *path, void **buf, size_t *size)
{
	size_t dsize;
	const void *data;

	if (!m || !path || !buf) { errno = EINVAL; return -1; }
	data = msys_search(m, path, &dsize);
	if (!data) return -1;

	if (m->hdr->flags & MSYS_F_ZLIB) {
		void *decomp = decompress(m, data, dsize, &dsize);
		if (!decomp) return -1;
		*buf = decomp;
		if (size) *size = dsize;
		return (int)dsize;
	}

	*buf = malloc(dsize ? dsize : 1);
	if (!*buf) return -1;
	memcpy(*buf, data, dsize);
	if (size) *size = dsize;
	return (int)dsize;
}
