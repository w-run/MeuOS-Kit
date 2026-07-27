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
	/* Free decompression buffers */
	struct msys_chunk *c = m->chunks;
	while (c) {
		struct msys_chunk *next = c->next;
		free(c->ptr);
		free(c);
		c = next;
	}
	free(m->entries);
	munmap(m->base, m->size);
	free(m);
}

/* ---- enumerate / count ---- */

uint32_t msys_count(struct msys *m)
{
	return m ? m->hdr->index_count : 0;
}

int msys_enumerate(struct msys *m, uint32_t idx,
                   const char **name, size_t *nlen, size_t *size)
{
	if (!m || !name || !nlen || !size) { errno = EINVAL; return -1; }
	if (idx >= m->hdr->index_count) { errno = ERANGE; return -1; }

	unsigned char *entry = m->entries[idx];
	uint64_t off   = read48(entry + 4);
	uint32_t dsize = read32(entry + 10);
	uint16_t nl    = read16(entry + 14);

	*name  = (const char *)(entry + 16);
	*nlen  = nl;
	*size  = dsize;

	/* Validate data offset is within file */
	if (off + dsize > m->size) { errno = EIO; return -1; }
	return 0;
}

/* ---- readdir (directory listing by prefix scan) ---- */

int msys_readdir(struct msys *m, const char *dir, msys_dir_cb cb, void *arg)
{
	if (!m || !dir || !cb) { errno = EINVAL; return -1; }

	size_t dlen = strlen(dir);
	uint32_t cnt = m->hdr->index_count;

	/* Simple linear dedup: track unique children seen so far.
	 * Typically few children per dir — O(N * seen) is fine. */
	const char **seen = NULL;
	size_t seen_cnt = 0, seen_cap = 0;

	for (uint32_t i = 0; i < cnt; i++) {
		unsigned char *entry = m->entries[i];
		uint16_t nl = read16(entry + 14);
		const char *ename = (const char *)(entry + 16);
		uint32_t dsize = read32(entry + 10);

		const char *child = ename;
		size_t child_len = nl;
		int is_dir = 0;

		if (dlen > 0) {
			/* Non-root: must start with dir prefix followed by '/' */
			if (nl <= dlen) continue;
			if (memcmp(ename, dir, dlen) != 0) continue;
			if (ename[dlen] != '/') continue;
			child = ename + dlen + 1;
			child_len = nl - dlen - 1;
		}

		/* Extract first path component */
		const char *slash = memchr(child, '/', child_len);
		if (slash) {
			child_len = (size_t)(slash - child);
			is_dir = 1;
		}

		if (child_len == 0) continue; /* skip trailing slash entries */

		/* Dedup: check if already reported */
		int dup = 0;
		for (size_t j = 0; j < seen_cnt; j++) {
			if (seen[j] && memcmp(seen[j], child, child_len) == 0) {
				dup = 1; break;
			}
		}
		if (dup) continue;

		/* Store for future dedup */
		if (seen_cnt >= seen_cap) {
			size_t nc = seen_cap ? seen_cap * 2 : 32;
			const char **ns = realloc(seen, nc * sizeof(*ns));
			if (!ns) { free(seen); errno = ENOMEM; return -1; }
			seen = ns;
			seen_cap = nc;
		}
		seen[seen_cnt] = NULL;
		unsigned char *copy = malloc(child_len);
		if (!copy) { free(seen); errno = ENOMEM; return -1; }
		memcpy(copy, child, child_len);
		seen[seen_cnt] = (const char *)copy;
		seen_cnt++;

		int ret = cb(child, child_len, is_dir ? 0 : dsize, is_dir, arg);
		/* Don't free seen strings yet — we need them for dedup */
		if (ret) {
			for (size_t j = 0; j < seen_cnt; j++) free((void *)seen[j]);
			free(seen);
			return 0;
		}
	}
	for (size_t j = 0; j < seen_cnt; j++) free((void *)seen[j]);
	free(seen);

	if (seen_cnt == 0) { errno = ENOENT; return -1; }
	return 0;
}

/* ---- fstat ---- */

int msys_fstat(struct msys *m, const char *name, size_t *size)
{
	size_t dsize;
	if (!msys_search(m, name, &dsize)) return -1;
	if (size) *size = dsize;
	return 0;
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

/* Decompress zlib data (dlopen libz.so.1).
 * Returns allocated buffer (caller must free) on success, NULL on error.
 * On success, *out_size receives decompressed size. */
static void *
decompress_zlib(struct msys *m, const void *compressed, size_t csize, size_t *out_size)
{
	(void)m;
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
			(*zlib_inflateEnd)(&strm);
			free(decomp); return NULL;
		}
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

/* Decompress zstd data (dlopen libzstd.so.1). */
static void *
decompress_zstd(struct msys *m, const void *compressed, size_t csize, size_t *out_size)
{
	(void)m;
	typedef size_t (*zstd_decompress_fn)(void *, size_t, const void *, size_t);
	typedef unsigned long long (*zstd_get_size_fn)(const void *, size_t);
	typedef unsigned (*zstd_is_error_fn)(size_t);

	static void *zstd_handle;
	static zstd_decompress_fn  zstd_decompress;
	static zstd_get_size_fn    zstd_getFrameContentSize;
	static zstd_is_error_fn    zstd_isError;
	static int zstd_loaded;

	if (!zstd_loaded) {
		zstd_loaded = 1;
		zstd_handle = dlopen("libzstd.so.1", RTLD_LAZY | RTLD_LOCAL);
		if (!zstd_handle) zstd_handle = dlopen("libzstd.so", RTLD_LAZY | RTLD_LOCAL);
		if (zstd_handle) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
			zstd_decompress        = (zstd_decompress_fn)dlsym(zstd_handle, "ZSTD_decompress");
			zstd_getFrameContentSize = (zstd_get_size_fn)dlsym(zstd_handle, "ZSTD_getFrameContentSize");
			zstd_isError           = (zstd_is_error_fn)dlsym(zstd_handle, "ZSTD_isError");
#pragma GCC diagnostic pop
		}
	}

	if (!zstd_handle || !zstd_decompress || !zstd_getFrameContentSize || !zstd_isError)
		return NULL;

	unsigned long long usize = (*zstd_getFrameContentSize)(compressed, csize);
	if ((*zstd_isError)(usize))
		return NULL;

	void *decomp = malloc((size_t)usize);
	if (!decomp) return NULL;

	size_t ret = (*zstd_decompress)(decomp, (size_t)usize, compressed, csize);
	if ((*zstd_isError)(ret)) {
		free(decomp);
		return NULL;
	}

	*out_size = (size_t)usize;
	return decomp;
}

/* Dispatch to the appropriate decompressor based on flags. */
static void *
decompress(struct msys *m, const void *data, size_t dsize, size_t *out_size)
{
	if (m->hdr->flags & MSYS_F_ZLIB)
		return decompress_zlib(m, data, dsize, out_size);
	if (m->hdr->flags & MSYS_F_ZSTD)
		return decompress_zstd(m, data, dsize, out_size);
	return NULL;
}

static int
register_chunk(struct msys *m, void *decomp)
{
	struct msys_chunk *c = malloc(sizeof(*c));
	if (!c) return -1;
	c->ptr = decomp;
	c->next = m->chunks;
	m->chunks = c;
	return 0;
}

FILE *msys_fopen(struct msys *m, const char *path, const char *mode)
{
	size_t dsize;
	const void *data;

	if (!m || !path) { errno = EINVAL; return NULL; }
	data = msys_search(m, path, &dsize);
	if (!data) return NULL;

	if (m->hdr->flags & (MSYS_F_ZLIB | MSYS_F_ZSTD)) {
		size_t usize;
		void *decomp = decompress(m, data, dsize, &usize);
		if (!decomp) return NULL;
		if (register_chunk(m, decomp) < 0) { free(decomp); return NULL; }
		return fmemopen(decomp, usize, mode);
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

	if (m->hdr->flags & (MSYS_F_ZLIB | MSYS_F_ZSTD)) {
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
