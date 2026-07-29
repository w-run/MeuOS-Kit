#include "mt/msys.h"
#include "sha256.h"

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
	if (memcmp(base, MSYS_MAGIC, MSYS_MAGIC_LEN) != 0 &&
	    memcmp(base, MSYS_MAGIC_V2, MSYS_MAGIC_LEN) != 0) {
		munmap(base, (size_t)st.st_size);
		errno = EINVAL;
		return NULL;
	}

	m = calloc(1, sizeof(*m));
	if (!m) { munmap(base, (size_t)st.st_size); return NULL; }

	m->base  = base;
	m->size  = (size_t)st.st_size;
	m->format_version = MSYS_FORMAT_V1;

	/* Detect format by magic */
	if (memcmp(base, MSYS_MAGIC, MSYS_MAGIC_LEN) == 0) {
		m->hdr = (struct msys_header *)base;
	} else if (memcmp(base, MSYS_MAGIC_V2, MSYS_MAGIC_LEN) == 0) {
		m->format_version = MSYS_FORMAT_V2;
		m->hdr_v2 = (struct msys_header_v2 *)base;
		/* v2 code path — handled below */
	} else {
		munmap(base, (size_t)st.st_size);
		free(m);
		errno = EINVAL;
		return NULL;
	}

	uint64_t index_off;
	uint32_t count;

	if (m->format_version == MSYS_FORMAT_V1) {
		index_off = m->hdr->index_offset;
		count = m->hdr->index_count;
	} else {
		index_off = m->hdr_v2->index_offset;
		count = m->hdr_v2->index_count;
	}

	/* Validate index offset fits within the mapped region. */
	if ((m->format_version == MSYS_FORMAT_V1 && index_off < sizeof(struct msys_header)) ||
	    (m->format_version == MSYS_FORMAT_V2 && index_off < sizeof(struct msys_header_v2)) ||
	    index_off >= m->size) {
		munmap(base, m->size); free(m);
		errno = EINVAL;
		return NULL;
	}

	if (m->format_version == MSYS_FORMAT_V1)
		m->index = (struct msys_index_entry *)((unsigned char *)base + index_off);
	else
		m->index_v2 = (struct msys_index_entry_v2 *)((unsigned char *)base + index_off);

	/* Build per-entry pointer array (entries are variable-length) */
	if (count > 0) {
		m->entries = calloc(count, sizeof(unsigned char *));
		if (!m->entries) {
			munmap(base, m->size); free(m);
			return NULL;
		}
		unsigned char *p = (m->format_version == MSYS_FORMAT_V1)
			? (unsigned char *)m->index
			: (unsigned char *)m->index_v2;
		uint64_t avail = m->size - index_off;
		for (uint32_t i = 0; i < count; i++) {
			m->entries[i] = p;
			if (m->format_version == MSYS_FORMAT_V1) {
				/* v1: 16 + name_len (uint16 at offset 14) */
				if (avail < 16) {
					munmap(base, m->size); free(m->entries); free(m);
					errno = EINVAL; return NULL;
				}
				uint16_t nlen = (uint16_t)p[14] | ((uint16_t)p[15] << 8);
				uint64_t ent = 16 + (uint64_t)nlen;
				if (avail < ent) {
					munmap(base, m->size); free(m->entries); free(m);
					errno = EINVAL; return NULL;
				}
				p += ent;
				avail -= ent;
			} else {
				/* v2: 32 + name_len (uint8 at offset 30) + optional 32 SHA-256 */
				if (avail < 32) {
					munmap(base, m->size); free(m->entries); free(m);
					errno = EINVAL; return NULL;
				}
				uint8_t nlen = p[30]; /* uint8 name_len */
				uint8_t chp  = p[31]; /* content_hash_present */
				uint64_t ent = 32 + (uint64_t)nlen + (chp ? 32ULL : 0);
				if (avail < ent) {
					munmap(base, m->size); free(m->entries); free(m);
					errno = EINVAL; return NULL;
				}
				p += ent;
				avail -= ent;
			}
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

/* ---- helpers for format-agnostic entry access ---- */
/* v1 entry: name_len=read16(off14), name=off16
 * v2 entry: name_len=uint8(off30),  name=off32
 * data_size at off10 is same for both. */

static inline uint16_t entry_nlen(const unsigned char *e, int v2)
{
	return v2 ? (uint16_t)e[30] : read16(e + 14);
}
static inline const char *entry_name(const unsigned char *e, int v2)
{
	return (const char *)(e + (v2 ? 32 : 16));
}
static inline uint32_t entry_dsize(const unsigned char *e, int v2)
{
	(void)v2;
	return read32(e + 10);
}
static inline uint64_t entry_doff(const unsigned char *e, int v2)
{
	(void)v2;
	return read48(e + 4);
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

	int v2 = (m->format_version == MSYS_FORMAT_V2);
	unsigned char *entry = m->entries[idx];

	*name  = entry_name(entry, v2);
	*nlen  = entry_nlen(entry, v2);
	*size  = entry_dsize(entry, v2);

	uint64_t off = entry_doff(entry, v2);
	if (off + *size > m->size) { errno = EIO; return -1; }
	return 0;
}

/* ---- readdir (directory listing by prefix scan) ---- */

int msys_readdir(struct msys *m, const char *dir, msys_dir_cb cb, void *arg)
{
	if (!m || !dir || !cb) { errno = EINVAL; return -1; }

	size_t dlen = strlen(dir);
	int v2 = (m->format_version == MSYS_FORMAT_V2);

	/* v2 with directory block: O(dir_count) hash lookup */
	if (v2 && m->hdr_v2->dir_count > 0) {
		uint32_t parent_hash = (dlen > 0)
			? msys_fnv1a((const unsigned char *)dir, dlen)
			: 0;
		uint16_t ph_trunc = (uint16_t)(parent_hash >> 16);
		unsigned char *dp = (unsigned char *)m->base + m->hdr_v2->dir_offset;
		uint32_t found = 0;

		for (uint32_t i = 0; i < m->hdr_v2->dir_count; i++) {
			uint16_t dph = (uint16_t)dp[0] | ((uint16_t)dp[1] << 8);
			uint8_t dnlen = dp[2];
			uint8_t dtype = dp[3];
			if (dph == ph_trunc) {
				found++;
				int is_dir = (dtype == MSYS_FILE_DIR);
				int ret = cb((const char *)(dp + 4), dnlen, 0, is_dir, arg);
				if (ret) return 0;
			}
			dp += 4 + dnlen;
		}
		if (found == 0) { errno = ENOENT; return -1; }
		return 0;
	}

	/* Fallback: v1 or v2 without dir block — O(N) prefix scan */
	uint32_t cnt = m->hdr->index_count;

	/* Simple linear dedup: track unique children seen so far.
	 * Typically few children per dir — O(N * seen) is fine. */
	const char **seen = NULL;
	size_t seen_cnt = 0, seen_cap = 0;

	for (uint32_t i = 0; i < cnt; i++) {
		unsigned char *entry = m->entries[i];
		uint16_t nl   = entry_nlen(entry, v2);
		const char *ename = entry_name(entry, v2);
		uint32_t dsize = entry_dsize(entry, v2);

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

/* ---- v2 API: format version, stat, readlink ---- */

int msys_format_version(struct msys *m)
{
	return m ? m->format_version : 0;
}

int msys_stat(struct msys *m, const char *name, struct msys_stat *st)
{
	size_t dsize;
	const void *data = msys_search(m, name, &dsize);
	if (!data) return -1;
	if (!st) return 0;

	memset(st, 0, sizeof(*st));
	st->size = dsize;
	st->name_hash = msys_fnv1a((const unsigned char *)name, strlen(name));

	if (m->format_version == MSYS_FORMAT_V2) {
		/* Search again to find the index entry for metadata */
		uint32_t target = st->name_hash;
		uint32_t lo = 0, hi = m->hdr->index_count;
		while (lo < hi) {
			uint32_t mid = lo + (hi - lo) / 2;
			unsigned char *e = m->entries[mid];
			uint32_t eh = read32(e);
			if (eh < target) { lo = mid + 1; continue; }
			if (eh > target) { hi = mid; continue; }
			/* Hash match — check name */
			uint32_t scan = mid;
			while (scan > lo) {
				if (read32(m->entries[scan - 1]) != target) break;
				scan--;
			}
			while (scan < m->hdr->index_count) {
				unsigned char *p = m->entries[scan];
				if (read32(p) != target) break;
				size_t nl = entry_nlen(p, 1);
				if (nl == strlen(name) &&
				    memcmp(entry_name(p, 1), name, nl) == 0) {
					st->file_type = (uint16_t)p[18] | ((uint16_t)p[19] << 8);
					st->mode      = (uint16_t)p[20] | ((uint16_t)p[21] << 8);
					st->uid       = read32(p + 22);
					st->gid       = read32(p + 26);
					st->mtime     = 0; /* stored in @mt entry */
					return 0;
				}
				scan++;
			}
			break;
		}
	}
	/* For v1 (or v2 entry not found via index), return size only */
	return 0;
}

int msys_readlink(struct msys *m, const char *name, char *buf, size_t bufsize)
{
	(void)buf; (void)bufsize;
	if (!m || !name || !buf) { errno = EINVAL; return -1; }
	if (bufsize == 0) { errno = EINVAL; return -1; }

	if (m->format_version == MSYS_FORMAT_V2) {
		/* Check file_type in index entry */
		uint32_t target = msys_fnv1a((const unsigned char *)name, strlen(name));
		uint32_t lo = 0, hi = m->hdr->index_count;
		while (lo < hi) {
			uint32_t mid = lo + (hi - lo) / 2;
			unsigned char *e = m->entries[mid];
			uint32_t eh = read32(e);
			if (eh < target) { lo = mid + 1; continue; }
			if (eh > target) { hi = mid; continue; }
			uint32_t scan = mid;
			while (scan > lo) {
				if (read32(m->entries[scan - 1]) != target) break;
				scan--;
			}
			while (scan < m->hdr->index_count) {
				unsigned char *p = m->entries[scan];
				if (read32(p) != target) break;
				size_t nl = entry_nlen(p, 1);
				if (nl == strlen(name) &&
				    memcmp(entry_name(p, 1), name, nl) == 0) {
					uint16_t ft = (uint16_t)p[18] | ((uint16_t)p[19] << 8);
					if (ft != MSYS_FILE_SYMLINK) {
						errno = EINVAL;
						return -1;
					}
					/* Read symlink target from data via msys_search (NOT msys_load,
					 * which resolves symlinks — we need the raw link target). */
					size_t sz2;
					const void *raw = msys_search(m, name, &sz2);
					if (!raw) return -1;
					size_t tocpy = sz2 < bufsize ? sz2 : bufsize - 1;
					memcpy(buf, raw, tocpy);
					buf[tocpy] = '\0';
					return (int)tocpy;
				}
				scan++;
			}
			break;
		}
	}
	errno = EINVAL;
	return -1;
}

/* ---- extended attributes ---- */

int msys_getxattr(struct msys *m, const char *name, const char *key,
                  char *buf, size_t bufsize)
{
	if (!m || !name || !key || !buf) { errno = EINVAL; return -1; }

	/* Build @xattr/<name> path */
	size_t nlen = strlen(name);
	size_t xlen = 7 + nlen; /* "@xattr/" prefix */
	char *xpath = malloc(xlen + 1);
	if (!xpath) return -1;
	memcpy(xpath, "@xattr/", 7);
	memcpy(xpath + 7, name, nlen);
	xpath[xlen] = '\0';

	size_t dsize;
	const void *data = msys_search(m, xpath, &dsize);
	free(xpath);
	if (!data) return -1;

	/* Parse key=value pairs (newline-separated) */
	const char *p = (const char *)data;
	size_t remaining = dsize;
	size_t klen = strlen(key);

	while (remaining > 0) {
		/* Find newline or end */
		const char *nl = memchr(p, '\n', remaining);
		size_t linelen = nl ? (size_t)(nl - p) : remaining;

		/* Find '=' separator */
		const char *eq = memchr(p, '=', linelen);
		if (eq) {
			size_t ekl = (size_t)(eq - p);
			if (ekl == klen && memcmp(p, key, klen) == 0) {
				size_t vlen = linelen - ekl - 1;
				if (vlen >= bufsize) vlen = bufsize - 1;
				memcpy(buf, eq + 1, vlen);
				buf[vlen] = '\0';
				return (int)vlen;
			}
		}

		if (!nl) break;
		remaining -= linelen + 1;
		p = nl + 1;
	}

	errno = ENOENT;
	return -1;
}

/* ---- verify (SHA-256 content integrity) ---- */

/* Forward declaration: decompress is defined after the verify section */
static void *decompress(struct msys *m, const void *data, size_t dsize, size_t *out_size);

int msys_verify(struct msys *m, const char *name)
{
	size_t dsize;
	const void *data = msys_search(m, name, &dsize);
	if (!data) return -1;

	/* For v2 entries with content_hash_present, verify SHA-256 */
	if (m->format_version == MSYS_FORMAT_V2) {
		uint32_t target = msys_fnv1a((const unsigned char *)name, strlen(name));
		uint32_t lo = 0, hi = m->hdr->index_count;
		while (lo < hi) {
			uint32_t mid = lo + (hi - lo) / 2;
			unsigned char *e = m->entries[mid];
			uint32_t eh = read32(e);
			if (eh < target) { lo = mid + 1; continue; }
			if (eh > target) { hi = mid; continue; }
			uint32_t scan = mid;
			while (scan > lo) {
				if (read32(m->entries[scan - 1]) != target) break;
				scan--;
			}
			while (scan < m->hdr->index_count) {
				unsigned char *p = m->entries[scan];
				if (read32(p) != target) break;
				uint8_t nlen = p[30];
				if ((size_t)nlen == strlen(name) &&
				    memcmp(p + 32, name, nlen) == 0) {
					if (p[31] == 0) return 0; /* no hash stored */
					/* Hash is at p + 32 + nlen */
					uint8_t stored[32];
					memcpy(stored, p + 32 + nlen, 32);
					uint8_t computed[32];
					uint32_t usz = read32(p + 14); /* uncompressed_size */
					if (usz != 0) {
						/* Data is compressed — decompress first */
						size_t dec_size;
						void *dec = decompress(m, data, dsize, &dec_size);
						if (!dec) return -1;
						sha256(dec, dec_size, computed);
						free(dec);
					} else {
						sha256(data, dsize, computed);
					}
					return memcmp(stored, computed, 32) == 0 ? 0 : -1;
				}
				scan++;
			}
			break;
		}
	}
	return 0; /* No hash stored — cannot verify */
}

int msys_verify_all(struct msys *m)
{
	if (!m) { errno = EINVAL; return -1; }
	uint32_t cnt = m->hdr->index_count;
	int v2 = (m->format_version == MSYS_FORMAT_V2);
	for (uint32_t i = 0; i < cnt; i++) {
		unsigned char *p = m->entries[i];
		uint8_t nlen = v2 ? p[30] : (uint8_t)(read16(p + 14));
		const char *name = v2 ? (const char *)(p + 32) : (const char *)(p + 16);

		/* Skip metadata entries */
		if (nlen > 0 && name[0] == '@') continue;

		if (v2 && p[31]) {
			size_t sz;
			const void *data = msys_search(m, name, &sz);
			if (!data) continue;
			uint8_t stored[32];
			memcpy(stored, p + 32 + nlen, 32);
			uint8_t computed[32];
			uint32_t usz = read32(p + 14);
			if (usz != 0) {
				size_t dec_sz;
				void *dec = decompress(m, data, sz, &dec_sz);
				if (!dec) continue;
				sha256(dec, dec_sz, computed);
				free(dec);
			} else {
				sha256(data, sz, computed);
			}
			if (memcmp(stored, computed, 32) != 0) {
				errno = EIO;
				return -1;
			}
		}
	}
	return 0;
}

/* ---- extension blocks ---- */

int msys_get_extension(struct msys *m, uint32_t type,
                        const void **data, uint32_t *dlen)
{
	if (!m || !data || !dlen) { errno = EINVAL; return -1; }
	if (m->format_version != MSYS_FORMAT_V2) { errno = ENOSYS; return -1; }

	uint32_t ext_off = m->hdr_v2->extension_offset;
	if (ext_off == 0) { errno = ENOENT; return -1; }

	unsigned char *p = (unsigned char *)m->base + ext_off;
	uint64_t avail = m->size - ext_off;

	while (avail >= 8) {
		uint32_t bt = read32(p);      /* block type */
		uint32_t bl = read32(p + 4);  /* block length */
		p += 8; avail -= 8;

		if (avail < bl) break; /* truncated block — stop */

		if (bt == type) {
			*data = p;
			*dlen = bl;
			return 0;
		}

		p += bl;
		avail -= bl;
	}

	errno = ENOENT;
	return -1;
}

/* ---- signature verification ---- */

#include "ed25519.h"

int msys_verify_signature(struct msys *m, const uint8_t pk[32])
{
	if (!m || !pk) { errno = EINVAL; return -1; }
	if (m->format_version != MSYS_FORMAT_V2) { errno = ENOSYS; return -1; }

	const void *sig_data = NULL;
	uint32_t sig_len = 0;
	if (msys_get_extension(m, 0x6e676973, &sig_data, &sig_len) < 0) {
		errno = ENOENT; /* no signature extension */
		return -1;
	}
	if (sig_len != 64) { errno = EINVAL; return -1; }

	/* Compute SHA-256 of the index block as the signed message */
	uint8_t index_hash[32];
	{
		uint64_t idx_off = (m->format_version == MSYS_FORMAT_V2)
			? m->hdr_v2->index_offset : m->hdr->index_offset;
		uint32_t idx_count = m->hdr->index_count;

		/* Compute index block size from entry-at-end parsing */
		/* Walk entries to find end of index */
		unsigned char *p = (unsigned char *)m->base + idx_off;
		uint64_t avail = m->size - idx_off;
		uint64_t idx_end = idx_off;
		int v2 = (m->format_version == MSYS_FORMAT_V2);

		for (uint32_t i = 0; i < idx_count; i++) {
			uint16_t nlen;
			if (v2) {
				if (avail < 32) break;
				nlen = p[30];
				uint8_t chp = p[31];
				p += 32 + nlen + (chp ? 32 : 0);
				avail -= 32 + nlen + (chp ? 32 : 0);
			} else {
				if (avail < 16) break;
				nlen = (uint16_t)p[14] | ((uint16_t)p[15] << 8);
				p += 16 + nlen;
				avail -= 16 + nlen;
			}
		}
		idx_end = (uint64_t)(p - (unsigned char *)m->base);

		sha256((const unsigned char *)m->base + idx_off,
		       (size_t)(idx_end - idx_off), index_hash);
	}

	/* Verify ed25519 signature */
	if (ed25519_verify(pk, index_hash, 32, (const uint8_t *)sig_data) != 1) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}

/* ---- search (binary search by name_hash, verify name string) ---- */

const void *msys_search(struct msys *m, const char *name, size_t *size)
{
	size_t name_len;
	uint32_t target_hash;
	uint32_t lo, hi;

	if (!m || !name) { errno = EINVAL; return NULL; }

	int v2 = (m->format_version == MSYS_FORMAT_V2);
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

				uint16_t ename_len = entry_nlen(p, v2);
				const char *ename   = entry_name(p, v2);
				if (ename_len == name_len &&
				    memcmp(ename, name, name_len) == 0) {
					/* Found! */
					uint64_t off = read48(p + 4);
					uint32_t dsize = read32(p + 10);
					if (size) *size = dsize;
					if (off + dsize > m->size) {
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

/* Forward declaration for symlink following */
static int msys_load_depth(struct msys *m, const char *path, void **buf,
                           size_t *size, int depth);

int msys_load(struct msys *m, const char *path, void **buf, size_t *size)
{
	return msys_load_depth(m, path, buf, size, 8);
}

/* Internal: msys_load with depth limit for symlink chain resolution */
static int
msys_load_depth(struct msys *m, const char *path, void **buf, size_t *size,
                int depth)
{
	size_t dsize;
	const void *data;

	if (!m || !path || !buf) { errno = EINVAL; return -1; }
	data = msys_search(m, path, &dsize);
	if (!data) return -1;

	/* Auto-follow symlinks for v2 archives */
	if (depth > 0 && m->format_version == MSYS_FORMAT_V2) {
		/* Find the index entry to check file_type */
		uint32_t target = msys_fnv1a((const unsigned char *)path, strlen(path));
		uint32_t lo = 0, hi = m->hdr->index_count;
		while (lo < hi) {
			uint32_t mid = lo + (hi - lo) / 2;
			unsigned char *e = m->entries[mid];
			uint32_t eh = read32(e);
			if (eh < target) { lo = mid + 1; continue; }
			if (eh > target) { hi = mid; continue; }
			uint32_t scan = mid;
			while (scan > lo) {
				if (read32(m->entries[scan - 1]) != target) break;
				scan--;
			}
			while (scan < m->hdr->index_count) {
				unsigned char *p = m->entries[scan];
				if (read32(p) != target) break;
				size_t nl = entry_nlen(p, 1);
				if (nl == strlen(path) &&
				    memcmp(entry_name(p, 1), path, nl) == 0) {
					uint16_t ft = (uint16_t)p[18] | ((uint16_t)p[19] << 8);
					if (ft == MSYS_FILE_SYMLINK) {
						/* Read link target and follow it */
						char linkbuf[4096];
						size_t linklen = dsize < sizeof(linkbuf) ? dsize : sizeof(linkbuf) - 1;
						memcpy(linkbuf, data, linklen);
						linkbuf[linklen] = '\0';
						return msys_load_depth(m, linkbuf, buf, size, depth - 1);
					}
					goto load_data;
				}
				scan++;
			}
			break;
		}
	}

load_data:
	if (m->hdr->flags & (MSYS_F_ZLIB | MSYS_F_ZSTD)) {
		if (dsize == 0) {
			/* Empty file — return empty allocation */
			*buf = malloc(1);
			if (!*buf) return -1;
			*(char *)*buf = '\0';
			if (size) *size = 0;
			return 0;
		}
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
