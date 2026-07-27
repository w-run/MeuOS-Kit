/*
 * mkmsys — .msys single-file sysroot packer
 *
 * Usage:
 *   mkmsys -o <output.msys> <root-directory>
 *   mkmsys --list <input.msys>
 *   mkmsys --extract <input.msys> -o <output-dir>
 *   mkmsys -o <output.msys> --arch <arch> <root-directory>
 *   mkmsys -o <output.msys> --compress=<type> <root-directory>
 *   mkmsys -o <output.msys> --incremental <root-directory>
 *
 * Options:
 *   -o <file>        Output .msys file path (or output dir with --extract)
 *   --list           List contents of an existing .msys file
 *   --extract        Extract contents of .msys to a directory
 *   --arch <name>    Write @meuos_arch metadata entry
 *   --compress=<t>   Compress data blocks: zlib, zstd (experimental)
 *   --incremental    Incremental mode: only repack changed files
 *   --help           Show this help message
 *
 * Compression modes:
 *   zlib   - DEFLATE compression via libz (loaded via dlopen)
 *   zstd   - Zstandard compression via libzstd (loaded via dlopen)
 *
 * Incremental mode:
 *   Reads existing .msys index, compares file mtime, and only repacks
 *   files whose mtime has changed. (Not yet implemented — currently
 *   falls back to full repack.)
 */

#include "mt/msys.h"
#include "sha256.h"
#include "ed25519.h"

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

/* ---- Minimal zlib struct for dynamic loading ---- */
/* Must match the real z_stream layout exactly for deflateInit_ version check */
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
#define Z_DEFAULT_COMPRESSION  (-1)
#define Z_FINISH 4
#define ZLIB_VER_MAJOR 1

/* ---- LE read/write helpers ---- */

static void wr48(uint8_t *b, uint64_t v) { b[0]=v; b[1]=v>>8; b[2]=v>>16; b[3]=v>>24; b[4]=v>>32; b[5]=v>>40; }
static void wr32(uint8_t *b, uint32_t v) { b[0]=v; b[1]=v>>8; b[2]=v>>16; b[3]=v>>24; }
static void wr16(uint8_t *b, uint16_t v) { b[0]=v; b[1]=v>>8; }

static void die(const char *msg) { perror(msg); exit(1); }

/* Round up to 4-byte alignment */
static inline size_t align4(size_t n) { return (n + 3) & ~(size_t)3; }

/* ---- entry collector ---- */

struct entry {
	uint32_t hash;
	char    *name;
	size_t   name_len;
	void    *data;
	size_t   data_size;
	time_t  mtime;
	uint16_t file_type;  /* MSYS_FILE_* */
	uint16_t mode;       /* file permissions */
	uint32_t uid;
	uint32_t gid;
	uint8_t  content_hash[32]; /* SHA-256 of data (for dedup/verify) */
};

struct collector {
	struct entry *entries;
	size_t        count;
	size_t        cap;
};

static void collector_add(struct collector *c, const char *relpath,
                          const void *data, size_t data_size, time_t mtime,
                          uint16_t file_type, uint16_t mode, uint32_t uid, uint32_t gid)
{
	if (c->count >= c->cap) {
		size_t newcap = c->cap ? c->cap * 2 : 4096;
		struct entry *p = realloc(c->entries, newcap * sizeof(struct entry));
		if (!p) die("realloc");
		c->entries = p;
		c->cap = newcap;
	}
	struct entry *e = &c->entries[c->count];
	e->name_len  = strlen(relpath);
	e->name      = strdup(relpath);
	if (!e->name) die("strdup");
	e->hash      = msys_fnv1a((const unsigned char *)e->name, e->name_len);
	e->data_size = data_size;
	e->data      = malloc(data_size ? data_size : 1);
	if (!e->data) die("malloc");
	if (data_size > 0) memcpy(e->data, data, data_size);
	if (data_size > 0)
		sha256(data, data_size, e->content_hash);
	else
		memset(e->content_hash, 0, 32);
	e->mtime     = mtime;
	e->file_type = file_type;
	e->mode      = mode;
	e->uid       = uid;
	e->gid       = gid;
	c->count++;
}

static void collector_walk(struct collector *c, const char *dir,
                           const char *rel_prefix)
{
	DIR *d = opendir(dir);
	if (!d) die(dir);

	struct dirent *de;
	while ((de = readdir(d)) != NULL) {
		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
			continue;

		size_t dlen = strlen(dir), nlen = strlen(de->d_name);
		size_t plen = strlen(rel_prefix);

		/* Absolute path */
		char *abspath = malloc(dlen + 1 + nlen + 1);
		memcpy(abspath, dir, dlen); abspath[dlen] = '/';
		memcpy(abspath + dlen + 1, de->d_name, nlen + 1);

		/* Relative path */
		char *relpath = malloc(plen + nlen + 2);
		if (plen > 0) {
			memcpy(relpath, rel_prefix, plen);
			relpath[plen] = '/';
			memcpy(relpath + plen + 1, de->d_name, nlen + 1);
		} else {
			memcpy(relpath, de->d_name, nlen + 1);
		}

		struct stat st;
		if (lstat(abspath, &st) < 0) { free(abspath); free(relpath); continue; }

		uint16_t file_type = MSYS_FILE_REG;
		uint16_t mode_bits = 0;
		uint32_t uid = 0, gid = 0;

		if (S_ISREG(st.st_mode)) {
			file_type = MSYS_FILE_REG;
			mode_bits = (uint16_t)(st.st_mode & 07777);
			uid = (uint32_t)st.st_uid;
			gid = (uint32_t)st.st_gid;
			FILE *fp = fopen(abspath, "rb");
			if (!fp) { free(abspath); free(relpath); continue; }
			void *buf = malloc(st.st_size ? (size_t)st.st_size : 1);
			size_t nread = 0;
			if (st.st_size > 0)
				nread = fread(buf, 1, (size_t)st.st_size, fp);
			fclose(fp);
			collector_add(c, relpath, buf, nread, st.st_mtime,
			              file_type, mode_bits, uid, gid);
			free(buf);
			/* Pack extended attributes as @xattr/<relpath> */
			{
				ssize_t xlist_len = llistxattr(abspath, NULL, 0);
				if (xlist_len > 0) {
					char *xlist = malloc((size_t)xlist_len);
					if (xlist && llistxattr(abspath, xlist, (size_t)xlist_len) > 0) {
						/* Build xattr data: key=value\nkey=value\n... */
						char xbuf[4096];
						size_t xpos = 0;
						char *xptr = xlist;
						while (xptr < xlist + xlist_len) {
							size_t kn = strlen(xptr);
							char xval[4096];
							ssize_t vl = getxattr(abspath, xptr, xval, sizeof(xval) - 1);
							if (vl > 0) {
								int needed = snprintf(xbuf + xpos, sizeof(xbuf) - xpos,
								                       "%s=%.*s\n", xptr, (int)vl, xval);
								if (needed > 0) xpos += (size_t)needed;
							}
							xptr += kn + 1;
						}
						if (xpos > 0) {
							size_t xname_len = 7 + strlen(relpath); /* @xattr/ */
							char *xname = malloc(xname_len + 1);
							if (xname) {
								memcpy(xname, "@xattr/", 7);
								memcpy(xname + 7, relpath, strlen(relpath) + 1);
								collector_add(c, xname, xbuf, xpos, 0, MSYS_FILE_REG, 0, 0, 0);
								free(xname);
							}
						}
					}
					free(xlist);
				}
			}
		} else if (S_ISDIR(st.st_mode)) {
			file_type = MSYS_FILE_DIR;
			mode_bits = (uint16_t)(st.st_mode & 07777);
			uid = (uint32_t)st.st_uid;
			gid = (uint32_t)st.st_gid;
			/* Directories are not stored as data entries */
			collector_walk(c, abspath, relpath);
		} else if (S_ISLNK(st.st_mode)) {
			file_type = MSYS_FILE_SYMLINK;
			mode_bits = (uint16_t)0777;
			uid = (uint32_t)st.st_uid;
			gid = (uint32_t)st.st_gid;
			/* Read symlink target and store as data */
			char linkbuf[4096];
			ssize_t llen = readlink(abspath, linkbuf, sizeof(linkbuf));
			if (llen > 0) {
				collector_add(c, relpath, linkbuf, (size_t)llen,
				              st.st_mtime, file_type, mode_bits, uid, gid);
			}
		} else if (S_ISCHR(st.st_mode)) {
			file_type = MSYS_FILE_CHR;
			collector_add(c, relpath, NULL, 0, st.st_mtime,
			              file_type, 0, 0, 0);
		} else if (S_ISBLK(st.st_mode)) {
			file_type = MSYS_FILE_BLK;
			collector_add(c, relpath, NULL, 0, st.st_mtime,
			              file_type, 0, 0, 0);
		} else if (S_ISFIFO(st.st_mode)) {
			file_type = MSYS_FILE_FIFO;
			collector_add(c, relpath, NULL, 0, st.st_mtime,
			              file_type, 0, 0, 0);
		}

		free(abspath);
		free(relpath);
	}
	closedir(d);
}

static int entry_cmp(const void *a, const void *b)
{
	const struct entry *ea = a, *eb = b;
	if (ea->hash < eb->hash) return -1;
	if (ea->hash > eb->hash) return 1;
	return strcmp(ea->name, eb->name);
}

static void collector_free(struct collector *c)
{
	for (size_t i = 0; i < c->count; i++) {
		free(c->entries[i].name);
		free(c->entries[i].data);
	}
	free(c->entries);
}

/* ---- write .msys ---- */

static void write_msys(const char *output, struct collector *c, uint32_t flags)
{
	FILE *fp = fopen(output, "wb");
	if (!fp) die(output);

	/* Layout:
	 *   [Header (32 bytes)]
	 *   [Data block 1]  (4-byte aligned)
	 *   ...
	 *   [Data block N]
	 *   [Index block]
	 */

	/* ---- compression via dlopen ---- */
	void *zlib_handle = NULL;
	int (*zlib_deflateInit)(void *, int, const char *, int) = NULL;
	int (*zlib_deflate)(void *, int) = NULL;
	int (*zlib_deflateEnd)(void *) = NULL;
	unsigned long (*zlib_compressBound)(unsigned long) = NULL;

	void *zstd_handle = NULL;
	size_t (*zstd_compress)(void *, size_t, const void *, size_t, int) = NULL;
	size_t (*zstd_compressBound)(size_t) = NULL;

	if (flags & MSYS_F_ZLIB) {
		zlib_handle = dlopen("libz.so.1", RTLD_LAZY | RTLD_LOCAL);
		if (!zlib_handle) zlib_handle = dlopen("libz.so", RTLD_LAZY | RTLD_LOCAL);
		if (!zlib_handle) {
			fprintf(stderr, "mkmsys: --compress=zlib but libz.so not found, "
			        "falling back to uncompressed\n");
			flags &= ~MSYS_F_ZLIB;
		} else {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
			zlib_deflateInit = (int (*)(void *, int, const char *, int))dlsym(zlib_handle, "deflateInit_");
			zlib_deflate     = (int (*)(void *, int))dlsym(zlib_handle, "deflate");
			zlib_deflateEnd  = (int (*)(void *))dlsym(zlib_handle, "deflateEnd");
			zlib_compressBound = (unsigned long (*)(unsigned long))dlsym(zlib_handle, "compressBound");
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
			if (!zlib_deflateInit || !zlib_deflate || !zlib_deflateEnd) {
				fprintf(stderr, "mkmsys: libz missing required symbols, "
				        "falling back to uncompressed\n");
				dlclose(zlib_handle); zlib_handle = NULL;
				flags &= ~MSYS_F_ZLIB;
			}
		}
	}

	if (flags & MSYS_F_ZSTD) {
		zstd_handle = dlopen("libzstd.so.1", RTLD_LAZY | RTLD_LOCAL);
		if (!zstd_handle) zstd_handle = dlopen("libzstd.so", RTLD_LAZY | RTLD_LOCAL);
		if (!zstd_handle) {
			fprintf(stderr, "mkmsys: --compress=zstd but libzstd.so not found, "
			        "falling back to uncompressed\n");
			flags &= ~MSYS_F_ZSTD;
		} else {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
			zstd_compress      = (size_t (*)(void *, size_t, const void *, size_t, int))dlsym(zstd_handle, "ZSTD_compress");
			zstd_compressBound = (size_t (*)(size_t))dlsym(zstd_handle, "ZSTD_compressBound");
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
			if (!zstd_compress || !zstd_compressBound) {
				fprintf(stderr, "mkmsys: libzstd missing required symbols, "
				        "falling back to uncompressed\n");
				dlclose(zstd_handle); zstd_handle = NULL;
				flags &= ~MSYS_F_ZSTD;
			}
		}
	}

	/* Add @mt/<name> metadata entries for incremental support */
	size_t meta_start = c->count;
	for (size_t i = 0; i < meta_start; i++) {
		struct entry *e = &c->entries[i];
		if (strncmp(e->name, "@", 1) == 0) continue; /* skip existing metadata */
		size_t mtname_len = e->name_len + 4;
		char *mtname = malloc(mtname_len + 1);
		if (!mtname) die("malloc");
		memcpy(mtname, "@mt/", 4);
		memcpy(mtname + 4, e->name, e->name_len);
		mtname[mtname_len] = '\0';
		uint8_t mtbuf[8] = {
			(uint8_t)e->mtime, (uint8_t)(e->mtime >> 8),
			(uint8_t)(e->mtime >> 16), (uint8_t)(e->mtime >> 24),
			(uint8_t)(e->mtime >> 32), (uint8_t)(e->mtime >> 40),
			(uint8_t)(e->mtime >> 48), (uint8_t)(e->mtime >> 56)
		};
		collector_add(c, mtname, mtbuf, 8, 0,
		              MSYS_FILE_REG, 0, 0, 0);
		free(mtname);
	}

	/* Re-sort after adding @mt/ entries */
	qsort(c->entries, c->count, sizeof(struct entry), entry_cmp);

	/* Phase 1: compute data sizes (compress each block) */
	unsigned char **compressed = NULL;
	size_t *comp_sizes = NULL;
	if ((flags & (MSYS_F_ZLIB | MSYS_F_ZSTD))) {
		int is_zlib = (flags & MSYS_F_ZLIB);
		compressed = calloc(c->count, sizeof(*compressed));
		comp_sizes = calloc(c->count, sizeof(*comp_sizes));
		if (!compressed || !comp_sizes) die("malloc");
		for (size_t i = 0; i < c->count; i++) {
			struct entry *e = &c->entries[i];
			if (e->data_size == 0) { comp_sizes[i] = 0; continue; }
			if (is_zlib) {
				unsigned long bound = (*zlib_compressBound)((unsigned long)e->data_size);
				compressed[i] = malloc(bound + 8);
				if (!compressed[i]) die("malloc");
				z_stream_min strm;
				memset(&strm, 0, sizeof(strm));
				strm.next_in = e->data;
				strm.avail_in = (unsigned int)e->data_size;
				strm.next_out = compressed[i];
				strm.avail_out = (unsigned int)(bound + 8);
				if ((*zlib_deflateInit)(&strm, Z_DEFAULT_COMPRESSION, "1.2.3", (int)sizeof(z_stream_min)) != Z_OK)
					die("deflateInit");
				int ret = (*zlib_deflate)(&strm, Z_FINISH);
				if (ret != Z_STREAM_END) die("deflate");
				(*zlib_deflateEnd)(&strm);
				comp_sizes[i] = strm.total_out;
			} else {
				/* zstd */
				size_t bound = (*zstd_compressBound)(e->data_size);
				compressed[i] = malloc(bound + 8);
				if (!compressed[i]) die("malloc");
				int level = 3; /* default compression level */
				size_t ret = (*zstd_compress)(compressed[i], bound + 8, e->data, e->data_size, level);
				if (zstd_handle) { /* check for errors using via NULL check — ZSTD_isError not loaded on writer side */
					/* ZSTD_compress returns error code if ret > bound: treat as failure */
				}
				comp_sizes[i] = ret;
			}
			if (comp_sizes[i] >= e->data_size) {
				free(compressed[i]);
				compressed[i] = NULL;
				comp_sizes[i] = e->data_size;
			}
		}
	}

	/* Phase 2: compute offsets */
	size_t hdr_size = sizeof(struct msys_header);
	size_t cur = hdr_size;
	size_t *data_offsets = malloc(c->count * sizeof(size_t));
	if (!data_offsets) die("malloc");

	for (size_t i = 0; i < c->count; i++) {
		cur = align4(cur);
		data_offsets[i] = cur;
		cur += (flags & (MSYS_F_ZLIB | MSYS_F_ZSTD) && compressed[i])
			? comp_sizes[i] : c->entries[i].data_size;
	}
	uint64_t index_offset = align4(cur);

	/* Phase 3: write header */
	struct msys_header hdr;
	memset(&hdr, 0, sizeof(hdr));
	memcpy(hdr.magic, MSYS_MAGIC, MSYS_MAGIC_LEN);
	hdr.index_offset = index_offset;
	hdr.index_count  = (uint32_t)c->count;
	hdr.flags        = flags;
	fwrite(&hdr, sizeof(hdr), 1, fp);

	/* Phase 4: write data blocks */
	for (size_t i = 0; i < c->count; i++) {
		struct entry *e = &c->entries[i];
		long pos = ftell(fp);
		while ((size_t)pos < align4((size_t)pos)) {
			fputc(0, fp); pos++;
		}
		if (e->data_size == 0) continue;
		if (flags & (MSYS_F_ZLIB | MSYS_F_ZSTD) && compressed[i]) {
			fwrite(compressed[i], 1, comp_sizes[i], fp);
		} else {
			fwrite(e->data, 1, e->data_size, fp);
		}
	}

	/* Pad index to alignment */
	{
		long pos = ftell(fp);
		while ((size_t)pos < align4((size_t)pos)) { fputc(0, fp); pos++; }
	}

	/* Phase 5: write index entries (sorted by hash, already sorted) */
	for (size_t i = 0; i < c->count; i++) {
		struct entry *e = &c->entries[i];
		uint64_t dsize = (flags & (MSYS_F_ZLIB | MSYS_F_ZSTD) && compressed[i])
			? comp_sizes[i] : e->data_size;
		uint8_t buf[16];
		wr32(buf,     e->hash);
		wr48(buf + 4, data_offsets[i]);
		wr32(buf + 10, (uint32_t)dsize);
		wr16(buf + 14, (uint16_t)e->name_len);
		fwrite(buf, 16, 1, fp);
		fwrite(e->name, 1, e->name_len, fp);
	}

	free(data_offsets);
	if (compressed) {
		for (size_t i = 0; i < c->count; i++) free(compressed[i]);
		free(compressed);
	}
	free(comp_sizes);
	if (zlib_handle) dlclose(zlib_handle);
	if (zstd_handle) dlclose(zstd_handle);
	fclose(fp);
}

/* ---- list ---- */

static int list_msys(const char *path)
{
	struct msys *m = msys_open(path);
	if (!m) die(path);

	printf(".msys file: %s\n", path);
	printf("Entries:    %u\n", msys_count(m));
	printf("Index off:  %lu\n",
	       (unsigned long)(m->format_version == MSYS_FORMAT_V2
	           ? m->hdr_v2->index_offset : m->hdr->index_offset));
	printf("Format:     v%d\n", msys_format_version(m));
	printf("Flags:      0x%08x\n", m->hdr->flags);
	printf("---\n");

	uint32_t count = msys_count(m);
	for (uint32_t i = 0; i < count; i++) {
		const char *name; size_t nlen, dsize;
		if (msys_enumerate(m, i, &name, &nlen, &dsize) < 0)
			continue;
		printf("  %s%.*s  (%zu bytes)\n",
		       nlen > 0 && name[nlen-1] == '/' ? "" : "",
		       (int)nlen, name, dsize);
	}

	msys_close(m);
	return 0;
}

/* ---- list-tree ---- */

/* Directory entry collected for tree display */
struct tree_node {
	char  *name;    /* immediate child name (not full path) */
	size_t nlen;
	size_t size;
	int    is_dir;
};

/* Callback: collect one tree_node into a realloc'd array.
 * arg points to { struct tree_node **array, size_t *count, size_t *cap }. */
struct collect_arg {
	struct tree_node **array;
	size_t *count, *cap;
};

static int collect_cb(const char *name, size_t nlen, size_t size, int is_dir, void *arg)
{
	struct collect_arg *ca = (struct collect_arg *)arg;
	if (*ca->count >= *ca->cap) {
		*ca->cap = *ca->cap ? *ca->cap * 2 : 64;
		struct tree_node *p = realloc(*ca->array, *ca->cap * sizeof(struct tree_node));
		if (!p) return 1;
		*ca->array = p;
	}
	struct tree_node *n = &(*ca->array)[(*ca->count)++];
	n->name = malloc(nlen);
	memcpy(n->name, name, nlen);
	n->nlen  = nlen;
	n->size  = size;
	n->is_dir = is_dir;
	return 0;
}

static void
print_tree(struct msys *m, const char *dir, int depth)
{
	struct tree_node *nodes = NULL;
	size_t count = 0, cap = 0;
	struct collect_arg ca = { &nodes, &count, &cap };

	if (msys_readdir(m, dir, collect_cb, &ca) < 0) {
		if (depth == 0) /* root always exists */
			fprintf(stderr, "(empty)\n");
		return;
	}

	for (size_t i = 0; i < count; i++) {
		struct tree_node *n = &nodes[i];
		/* Skip @mt/ metadata entries */
		if (n->nlen >= 4 && memcmp(n->name, "@mt/", 4) == 0) continue;
		if (n->nlen == 3 && memcmp(n->name, "@mt", 3) == 0) continue;
		printf("%*s", depth * 2, "");
		if (n->is_dir)
			printf("%.*s/\n", (int)n->nlen, n->name);
		else
			printf("%.*s  (%zu bytes)\n", (int)n->nlen, n->name, n->size);

		if (n->is_dir) {
			/* Recurse into subdirectory */
			size_t dlen = strlen(dir);
			size_t sublen = dlen + 1 + n->nlen;
			char *sub = malloc(sublen + 1);
			if (dlen > 0) {
				memcpy(sub, dir, dlen);
				sub[dlen] = '/';
				memcpy(sub + dlen + 1, n->name, n->nlen);
			} else {
				memcpy(sub, n->name, n->nlen);
			}
			sub[sublen] = '\0';
			print_tree(m, sub, depth + 1);
			free(sub);
		}
	}

	for (size_t i = 0; i < count; i++) free(nodes[i].name);
	free(nodes);
}

static int
list_tree_msys(const char *path)
{
	struct msys *m = msys_open(path);
	if (!m) die(path);

	printf(".msys file: %s\n", path);
	printf("Entries:    %u\n", msys_count(m));
	printf("Flags:      0x%08x\n", m->hdr->flags);
	printf("\n");

	print_tree(m, "", 0);
	msys_close(m);
	return 0;
}

/* ---- extract ---- */

static void ensure_parent(const char *path)
{
	char *p = strdup(path);
	if (!p) die("strdup");
	for (char *s = p + 1; *s; s++) {
		if (*s == '/') {
			*s = '\0';
			mkdir(p, 0755);
			*s = '/';
		}
	}
	free(p);
}

static int extract_msys(const char *msys_path, const char *outdir)
{
	struct msys *m = msys_open(msys_path);
	if (!m) die(msys_path);

	uint32_t cnt = msys_count(m);
	size_t extracted = 0;

	for (uint32_t i = 0; i < cnt; i++) {
		const char *name; size_t nlen, dsize;
		if (msys_enumerate(m, i, &name, &nlen, &dsize) < 0)
			continue;

		/* Skip @mt/ metadata entries */
		if (nlen > 4 && memcmp(name, "@mt/", 4) == 0) continue;

		/* Build output path: outdir/name */
		size_t odlen = strlen(outdir);
		char *path = malloc(odlen + 1 + nlen + 1);
		if (!path) die("malloc");
		memcpy(path, outdir, odlen);
		path[odlen] = '/';
		memcpy(path + odlen + 1, name, nlen);
		path[odlen + 1 + nlen] = '\0';

		ensure_parent(path);

		if (dsize > 0) {
			void *buf = NULL;
			if (msys_load(m, name, &buf, NULL) < 0) {
				fprintf(stderr, "extract: failed to load '%.*s'\n", (int)nlen, name);
				free(path);
				continue;
			}
			FILE *fp = fopen(path, "wb");
			if (!fp) { perror(path); free(buf); free(path); continue; }
			fwrite(buf, 1, dsize, fp);
			fclose(fp);
			free(buf);
		} else {
			/* Empty file */
			FILE *fp = fopen(path, "wb");
			if (fp) fclose(fp);
		}

		free(path);
		extracted++;
	}

	printf("Extracted %zu entries to %s\n", extracted, outdir);
	msys_close(m);
	return 0;
}

	/* ---- write v2 .msys ---- */

static void write_msys_v2(const char *output, struct collector *c, uint32_t flags,
                          const void *ext_data, uint32_t ext_len,
                          const uint8_t sign_sk[64], const uint8_t sign_pk[32])
{
	(void)sign_pk; /* public key stored separately in key file for verification */
	FILE *fp = fopen(output, "w+b");
	if (!fp) die(output);

	/* Layout:
	 *   [Header (64 bytes)]
	 *   [Data block 1..N]
	 *   [@mt/ metadata entries appended to collector]
	 *   [Index block (v2 entries, 32+name+opt bytes)]
	 */

	/* Compression (same as v1) */
	void *zlib_handle = NULL;
	void *zstd_handle = NULL;
	int (*zlib_deflateInit)(void *, int, const char *, int) = NULL;
	int (*zlib_deflate)(void *, int) = NULL;
	int (*zlib_deflateEnd)(void *) = NULL;
	unsigned long (*zlib_compressBound)(unsigned long) = NULL;
	size_t (*zstd_compress)(void *, size_t, const void *, size_t, int) = NULL;
	size_t (*zstd_compressBound)(size_t) = NULL;

	if (flags & MSYS_F_ZLIB) {
		zlib_handle = dlopen("libz.so.1", RTLD_LAZY | RTLD_LOCAL);
		if (!zlib_handle) zlib_handle = dlopen("libz.so", RTLD_LAZY | RTLD_LOCAL);
		if (!zlib_handle) {
			fprintf(stderr, "mkmsys: --compress=zlib but libz.so not found\n");
			exit(1);
		}
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
		zlib_deflateInit = (int (*)(void *, int, const char *, int))dlsym(zlib_handle, "deflateInit_");
		zlib_deflate     = (int (*)(void *, int))dlsym(zlib_handle, "deflate");
		zlib_deflateEnd  = (int (*)(void *))dlsym(zlib_handle, "deflateEnd");
		zlib_compressBound = (unsigned long (*)(unsigned long))dlsym(zlib_handle, "compressBound");
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
		if (!zlib_deflateInit || !zlib_deflate || !zlib_deflateEnd || !zlib_compressBound)
			die("missing zlib symbols");
	}
	if (flags & MSYS_F_ZSTD) {
		zstd_handle = dlopen("libzstd.so.1", RTLD_LAZY | RTLD_LOCAL);
		if (!zstd_handle) zstd_handle = dlopen("libzstd.so", RTLD_LAZY | RTLD_LOCAL);
		if (!zstd_handle) {
			fprintf(stderr, "mkmsys: --compress=zstd but libzstd.so not found\n");
			exit(1);
		}
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
		zstd_compress = (size_t (*)(void *, size_t, const void *, size_t, int))dlsym(zstd_handle, "ZSTD_compress");
		zstd_compressBound = (size_t (*)(size_t))dlsym(zstd_handle, "ZSTD_compressBound");
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
		if (!zstd_compress || !zstd_compressBound)
			die("missing zstd symbols");
	}

	/* Add @mt/<name> metadata entries for incremental support */
	size_t meta_start = c->count;
	for (size_t i = 0; i < meta_start; i++) {
		struct entry *e = &c->entries[i];
		if (strncmp(e->name, "@", 1) == 0) continue;
		size_t mtname_len = e->name_len + 4;
		char *mtname = malloc(mtname_len + 1);
		if (!mtname) die("malloc");
		memcpy(mtname, "@mt/", 4);
		memcpy(mtname + 4, e->name, e->name_len);
		mtname[mtname_len] = '\0';
		uint8_t mtbuf[8];
		uint64_t mt = (uint64_t)e->mtime;
		for (int j = 0; j < 8; j++) { mtbuf[j] = mt & 0xff; mt >>= 8; }
		collector_add(c, mtname, mtbuf, 8, 0, MSYS_FILE_REG, 0, 0, 0);
		free(mtname);
	}
	qsort(c->entries, c->count, sizeof(struct entry), entry_cmp);

	/* Phase 1: compute data sizes (compress each block if needed) */
	int is_zlib = (flags & MSYS_F_ZLIB);
	int is_zstd = (flags & MSYS_F_ZSTD);
	unsigned char **compressed = NULL;
	size_t *comp_sizes = NULL;
	if (is_zlib || is_zstd) {
		compressed = calloc(c->count, sizeof(*compressed));
		comp_sizes = calloc(c->count, sizeof(*comp_sizes));
		if (!compressed || !comp_sizes) die("malloc");
		for (size_t i = 0; i < c->count; i++) {
			struct entry *e = &c->entries[i];
			if (e->data_size == 0) { comp_sizes[i] = 0; continue; }
			if (is_zlib) {
				unsigned long bound = (*zlib_compressBound)((unsigned long)e->data_size);
				compressed[i] = malloc(bound + 8);
				if (!compressed[i]) die("malloc");
				z_stream_min strm;
				memset(&strm, 0, sizeof(strm));
				strm.next_in = e->data;
				strm.avail_in = (unsigned int)e->data_size;
				strm.next_out = compressed[i];
				strm.avail_out = (unsigned int)(bound + 8);
				if ((*zlib_deflateInit)(&strm, Z_DEFAULT_COMPRESSION, "1.2.3", (int)sizeof(z_stream_min)) != Z_OK)
					die("deflateInit");
				if ((*zlib_deflate)(&strm, Z_FINISH) != Z_STREAM_END) die("deflate");
				(*zlib_deflateEnd)(&strm);
				comp_sizes[i] = strm.total_out;
				if (comp_sizes[i] >= e->data_size) {
					free(compressed[i]); compressed[i] = NULL;
					comp_sizes[i] = e->data_size;
				}
			} else {
				size_t bound = (*zstd_compressBound)(e->data_size);
				compressed[i] = malloc(bound + 8);
				if (!compressed[i]) die("malloc");
				size_t out = (*zstd_compress)(compressed[i], bound, e->data, e->data_size, 3);
				if (out >= e->data_size) {
					free(compressed[i]); compressed[i] = NULL;
					comp_sizes[i] = e->data_size;
				} else {
					comp_sizes[i] = out;
				}
			}
		}
	}

	/* Phase 2: build directory block + compute offsets */

	/* Build directory block: for each entry, extract parent->child mapping */
	/* Format: parent_hash_trunc[2] | name_len[1] | entry_type[1] | name[] */
	unsigned char *dir_block = NULL;
	size_t dir_block_size = 0, dir_block_cap = 0;
	uint32_t dir_count = 0;

	for (size_t i = 0; i < c->count; i++) {
		struct entry *e = &c->entries[i];
		if (e->name[0] == '@' && e->name_len > 0 && e->name[0] == '@')
			continue; /* skip metadata entries */
		/* Extract each path component */
		char pathcopy[4096];
		if (e->name_len >= sizeof(pathcopy)) continue;
		memcpy(pathcopy, e->name, e->name_len);
		pathcopy[e->name_len] = '\0';
		/* Walk path components */
		char *part = pathcopy;
		while (part && *part) {
			char *slash = strchr(part, '/');
			if (slash) *slash = '\0';
			/* Compute parent hash (hash of everything before this component) */
			size_t parent_len = (size_t)(part - pathcopy);
			uint32_t parent_hash = 0;
			if (parent_len > 0) {
				/* Remove trailing slash for hash */
				parent_hash = msys_fnv1a((const unsigned char *)pathcopy, parent_len - 1);
			}
			/* parent_hash_trunc = top 16 bits */
			uint16_t ph_trunc = (uint16_t)(parent_hash >> 16);
			uint8_t nlen = (uint8_t)strlen(part);
			uint8_t entry_type = slash ? MSYS_FILE_DIR : (uint8_t)e->file_type;

			/* Dedup: check if this (ph_trunc, name) already exists */
			int dup = 0;
			unsigned char *dp = dir_block;
			for (uint32_t d = 0; d < dir_count; d++) {
				uint16_t dph = (uint16_t)dp[0] | ((uint16_t)dp[1] << 8);
				uint8_t dnlen = dp[2];
				if (dph == ph_trunc && dnlen == nlen &&
				    memcmp(dp + 4, part, nlen) == 0) {
					dup = 1; break;
				}
				dp += 4 + dnlen;
			}
			if (dup) { if (slash) { *slash = '/'; part = slash + 1; } else break; continue; }

			/* Allocate */
			size_t ent_sz = 4 + nlen;
			if (dir_block_size + ent_sz > dir_block_cap) {
				size_t nc = dir_block_cap ? dir_block_cap * 2 : 256;
				unsigned char *ns = realloc(dir_block, nc);
				if (!ns) die("realloc");
				dir_block = ns;
				dir_block_cap = nc;
			}
			/* Write: parent_hash_trunc[2] + name_len[1] + entry_type[1] + name[] */
			dir_block[dir_block_size]     = ph_trunc & 0xff;
			dir_block[dir_block_size + 1] = (ph_trunc >> 8) & 0xff;
			dir_block[dir_block_size + 2] = nlen;
			dir_block[dir_block_size + 3] = entry_type;
			memcpy(dir_block + dir_block_size + 4, part, nlen);
			dir_block_size += ent_sz;
			dir_count++;

			if (slash) { *slash = '/'; part = slash + 1; }
			else break;
		}
	}

	size_t hdr_size = sizeof(struct msys_header_v2);
	size_t cur = hdr_size;
	size_t *data_offsets = malloc(c->count * sizeof(size_t));
	if (!data_offsets) die("malloc");

	/* Dedup: map SHA-256 hash -> first entry index that stored this content */
	int dedup = (flags & MSYS_F_DEDUP);
	int *dedup_map = NULL;
	if (dedup) {
		dedup_map = calloc(c->count, sizeof(int));
		if (!dedup_map) die("malloc");
		for (size_t i = 0; i < c->count; i++)
			dedup_map[i] = -1;
		/* First pass: find duplicates by SHA-256 */
		for (size_t i = 0; i < c->count; i++) {
			if (c->entries[i].data_size == 0) continue;
			if (c->entries[i].name[0] == '@') continue;
			for (size_t j = 0; j < i; j++) {
				if (c->entries[j].data_size == c->entries[i].data_size &&
				    memcmp(c->entries[j].content_hash, c->entries[i].content_hash, 32) == 0) {
					dedup_map[i] = (int)j;
					break;
				}
			}
		}
	}

	int streaming = (flags & MSYS_F_STREAMING);

	for (size_t i = 0; i < c->count; i++) {
		cur = align4(cur);
		struct entry *e = &c->entries[i];
		/* Streaming inline header: [nlen:2][dsize:4][name:nlen] before data */
		size_t stream_oh = 0;
		if (streaming && !(dedup && dedup_map[i] >= 0))
			stream_oh = 6 + e->name_len;
		/* If this entry is a dedup of an earlier one, use that offset */
		if (dedup && dedup_map[i] >= 0) {
			data_offsets[i] = data_offsets[(size_t)dedup_map[i]];
		} else {
			data_offsets[i] = cur + stream_oh;
		}
		cur += stream_oh + ((is_zlib || is_zstd) && compressed[i]
			? comp_sizes[i] : e->data_size);
	}
	uint64_t dir_offset_ftell; /* computed after writing data, from actual file pos */
	uint64_t index_offset;     /* computed after writing data, from actual file pos */

	/* ---- write v2 header placeholder ---- */
	struct msys_header_v2 hdr;
	memset(&hdr, 0, sizeof(hdr));
	memcpy(hdr.magic, MSYS_MAGIC_V2, 8);
	hdr.index_offset = 0; /* placeholder */
	hdr.index_count  = 0; /* placeholder */
	hdr.flags        = 0; /* placeholder */
	hdr.dir_offset   = 0; /* placeholder */
	hdr.dir_count    = 0; /* placeholder */
	hdr.extension_offset = 0;
	hdr.data_size_total  = 0;
	hdr.content_hash     = 0;
	/* Write temporary header — we'll seek back and rewrite after computing real offsets */
	fwrite(&hdr, sizeof(hdr), 1, fp);

	/* Phase 4: write data blocks (skip dedup duplicates) */
	for (size_t i = 0; i < c->count; i++) {
		struct entry *e = &c->entries[i];
		/* Skip if this is a dedup of an earlier entry */
		if (dedup && dedup_map[i] >= 0) continue;
		long pos = ftell(fp);
		while ((size_t)pos < align4((size_t)pos)) { fputc(0, fp); pos++; }
		if (e->data_size == 0) continue;
		/* Streaming: write inline header [nlen:2][name:nlen][dsize:4] before data */
		if (streaming) {
			uint8_t ih[4];
			uint16_t nlen16 = (uint16_t)e->name_len;
			uint32_t dsize32 = (uint32_t)(((is_zlib || is_zstd) && compressed[i])
				? comp_sizes[i] : e->data_size);
			wr16(ih, nlen16);
			fwrite(ih, 1, 2, fp);
			fwrite(e->name, 1, nlen16, fp);
			wr32(ih, dsize32);
			fwrite(ih, 1, 4, fp);
		}
		if ((is_zlib || is_zstd) && compressed[i])
			fwrite(compressed[i], 1, comp_sizes[i], fp);
		else
			fwrite(e->data, 1, e->data_size, fp);
	}

	/* Phase 4.5: write directory block (if any), compute real offsets from ftell */
	if (dir_block) {
		dir_offset_ftell = (uint64_t)ftell(fp);
		while (dir_offset_ftell < align4((size_t)dir_offset_ftell)) {
			fputc(0, fp); dir_offset_ftell++;
		}
		fwrite(dir_block, 1, dir_block_size, fp);
		free(dir_block);
	} else {
		dir_offset_ftell = 0;
	}

	/* Index offset: compute from actual file position */
	index_offset = (uint64_t)ftell(fp);
	while (index_offset < align4((size_t)index_offset)) {
		fputc(0, fp); index_offset++;
	}

	/* Phase 5: write v2 index entries */
	uint64_t ext_offset_value = 0;
	{
		long pos = ftell(fp);
		while ((size_t)pos < align4((size_t)pos)) { fputc(0, fp); pos++; }
	}
	for (size_t i = 0; i < c->count; i++) {
		struct entry *e = &c->entries[i];
		uint64_t dsize = ((is_zlib || is_zstd) && compressed[i])
			? comp_sizes[i] : e->data_size;
		uint8_t buf[32];
		memset(buf, 0, 32);
		/* name_hash[4] */
		buf[0] = e->hash; buf[1] = e->hash >> 8;
		buf[2] = e->hash >> 16; buf[3] = e->hash >> 24;
		/* data_offset[6] */
		wr48(buf + 4, data_offsets[i]);
		/* data_size[4] */
		wr32(buf + 10, (uint32_t)dsize);
		/* uncompressed_size[4] */
		wr32(buf + 14, (uint32_t)e->data_size);
		/* file_type[2] */
		buf[18] = e->file_type; buf[19] = e->file_type >> 8;
		/* mode[2] */
		buf[20] = e->mode; buf[21] = e->mode >> 8;
		/* uid[4] */
		wr32(buf + 22, e->uid);
		/* gid[4] */
		wr32(buf + 26, e->gid);
		/* name_len */
		buf[30] = (uint8_t)e->name_len;
		/* content_hash_present: 1 if dedup flag set or entry has data */
		buf[31] = (dedup || e->data_size > 0) ? 1 : 0;
		fwrite(buf, 32, 1, fp);
		fwrite(e->name, 1, e->name_len, fp);
		/* Write SHA-256 hash if content_hash_present */
		if (buf[31])
			fwrite(e->content_hash, 1, 32, fp);
	}

	/* Phase 6: write extension blocks (after index) */
	ext_offset_value = 0;

	/* If secret key provided, compute ed25519 signature over the index block */
	uint8_t sig_buf[64];
	int have_sig = 0;
	if (sign_sk) {
		fflush(fp); /* ensure index is flushed before reading back */
		long idx_end_pos = ftell(fp); /* current position = end of index */
		size_t idx_size = (size_t)(idx_end_pos - (long)index_offset);

		uint8_t *idx_data = malloc(idx_size);
		if (idx_data) {
			fseek(fp, (long)index_offset, SEEK_SET);
			fread(idx_data, 1, idx_size, fp);
			uint8_t idx_hash[32];
			sha256(idx_data, idx_size, idx_hash);
			free(idx_data);
			ed25519_sign(sign_sk, idx_hash, 32, sig_buf);
			have_sig = 1;
		}
		fseek(fp, 0, SEEK_END); /* back to end for extension write */
	}

	if (have_sig || (ext_data && ext_len > 0)) {
		ext_offset_value = (uint64_t)ftell(fp);
		while (ext_offset_value < align4((size_t)ext_offset_value)) {
			fputc(0, fp); ext_offset_value++;
		}
	}
	if (have_sig) {
		uint8_t ext_hdr[8];
		wr32(ext_hdr,     0x6e676973); /* "sign" fourcc */
		wr32(ext_hdr + 4, 64);
		fwrite(ext_hdr, 8, 1, fp);
		fwrite(sig_buf, 1, 64, fp);
	}
	if (ext_data && ext_len > 0) {
		uint8_t ext_hdr[8];
		wr32(ext_hdr,     0x6e676973); /* "sign" fourcc */
		wr32(ext_hdr + 4, ext_len);
		fwrite(ext_hdr, 8, 1, fp);
		fwrite(ext_data, 1, ext_len, fp);
	}

	/* Seek back and rewrite header with real final offsets */
	{
		struct msys_header_v2 hdr2;
		memset(&hdr2, 0, sizeof(hdr2));
		memcpy(hdr2.magic, MSYS_MAGIC_V2, 8);
		hdr2.index_offset = index_offset;
		hdr2.index_count  = (uint32_t)c->count;
		hdr2.flags        = flags | MSYS_F_DIR_BLOCK;
		hdr2.dir_offset   = dir_offset_ftell;
		hdr2.dir_count    = dir_count;
		hdr2.extension_offset = ext_offset_value;
		hdr2.data_size_total  = 0;
		hdr2.content_hash     = 0;
		fseek(fp, 0, SEEK_SET);
		fwrite(&hdr2, sizeof(hdr2), 1, fp);
	}

	free(data_offsets);
	free(dedup_map);
	if (compressed) {
		for (size_t i = 0; i < c->count; i++) free(compressed[i]);
		free(compressed);
	}
	free(comp_sizes);
	if (zlib_handle) dlclose(zlib_handle);
	if (zstd_handle) dlclose(zstd_handle);
	fclose(fp);
}

/* ---- add metadata entry ---- */

static void add_metadata_entry(struct collector *c, const char *key,
                               const char *value)
{
	collector_add(c, key, value, strlen(value), 0,
	              MSYS_FILE_REG, 0, 0, 0);
}

/* ---- main ---- */

int main(int argc, char *argv[])
{
	const char *output = NULL;
	const char *arch = NULL;
	int list_mode = 0;
	int tree_mode = 0;
	int extract_mode = 0;
	const char *input = NULL;
	int incremental = 0;
	int dedup_mode = 0;
	int streaming_mode = 0;
	const char *sign_keyfile = NULL;
	int format_v2 = 0;
	const char *compress = NULL;

	static const char *usage_short =
	  "Usage: mkmsys -o <output> [options] <root-dir>\n"
	  "       mkmsys --list <input.msys>\n"
	  "       mkmsys --list-tree <input.msys>\n"
	  "       mkmsys --extract <input.msys> -o <dir>\n"
	  "Try `mkmsys --help` for more information.\n";
	static const char *usage_full =
	  "Usage: mkmsys -o <output> [options] <root-dir>\n"
	  "       mkmsys --list <input.msys>\n"
	  "       mkmsys --list-tree <input.msys>\n"
	  "       mkmsys --extract <input.msys> -o <dir>\n"
	  "\n"
	  "Options:\n"
	  "  -o <file>          Output .msys file path (or output dir with --extract)\n"
	  "  --list             List contents of an existing .msys file (flat index)\n"
	  "  --list-tree        List contents with directory tree structure\n"
	  "  --extract          Extract contents of .msys to directory\n"
	  "  --arch <name>      Write @meuos_arch metadata entry\n"
	  "  --compress=<type>  Compress data blocks: zlib, zstd\n"
	  "  --incremental      Incremental mode: only repack changed files\n"
	  "  --dedup            Content dedup (v2 only): SHA-256 identical data stored once\n"
	  "  --sign=<keyfile>   Sign the index with ed25519 secret key (v2 only)\n"
	  "  --format <v1|v2>   Output format version (default: v1)\n"
	  "  --help             Show this help message\n"
	  "\n"
	  "Compression types:\n"
	  "  zlib   DEFLATE compression via libz (loaded via dlopen)\n"
	  "  zstd   Zstandard compression via libzstd (loaded via dlopen)\n"
	  "\n"
	  "Incremental mode compares file mtime against the existing .msys\n"
	  "archive and only repacks files that have changed.\n";

	int i = 1;
	while (i < argc) {
		if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
			output = argv[i + 1]; i += 2;
		} else if (strcmp(argv[i], "--list") == 0) {
			list_mode = 1; i++;
		} else if (strcmp(argv[i], "--list-tree") == 0) {
			tree_mode = 1; i++;
		} else if (strcmp(argv[i], "--extract") == 0) {
			extract_mode = 1; i++;
		} else if (strcmp(argv[i], "--arch") == 0 && i + 1 < argc) {
			arch = argv[i + 1]; i += 2;
		} else if (strcmp(argv[i], "--incremental") == 0) {
			incremental = 1; i++;
		} else if (strcmp(argv[i], "--dedup") == 0) {
			dedup_mode = 1; i++;
		} else if (strcmp(argv[i], "--streaming") == 0) {
			format_v2 = 1; streaming_mode = 1; i++;
		} else if (strncmp(argv[i], "--sign=", 7) == 0) {
			sign_keyfile = argv[i] + 7; i++;
		} else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
			format_v2 = (strcmp(argv[i + 1], "v2") == 0); i += 2;
		} else if (strcmp(argv[i], "--help") == 0) {
			printf("%s", usage_full);
			return 0;
		} else if (strncmp(argv[i], "--compress=", 11) == 0) {
			compress = argv[i] + 11; i++;
		} else if (input == NULL) {
			input = argv[i]; i++;
		} else {
			fprintf(stderr, "%s", usage_short);
			return 1;
		}
	}

	if (list_mode) {
		if (!input) input = output;
		if (!input) { fprintf(stderr, "Usage: mkmsys --list <input.msys>\n"); return 1; }
		return list_msys(input);
	}

	if (tree_mode) {
		if (!input) input = output;
		if (!input) { fprintf(stderr, "Usage: mkmsys --list-tree <input.msys>\n"); return 1; }
		return list_tree_msys(input);
	}

	if (extract_mode) {
		if (!input) input = output;
		if (!input) { fprintf(stderr, "Usage: mkmsys --extract <input.msys> -o <outdir>\n"); return 1; }
		return extract_msys(input, output ? output : ".");
	}

	if (!output || !input) {
		fprintf(stderr, "%s", usage_short);
		return 1;
	}

	/* Resolve flags */
	uint32_t flags = MSYS_F_NONE;

	if (compress) {
		if (strcmp(compress, "zlib") == 0) {
			flags |= MSYS_F_ZLIB;
		} else if (strcmp(compress, "zstd") == 0) {
			flags |= MSYS_F_ZSTD;
		} else {
			fprintf(stderr, "mkmsys: unknown compression type '%s'\n"
			        "Supported: zlib, zstd\n", compress);
			return 1;
		}
	}

	if (incremental) {
		flags |= MSYS_F_INCREMENTAL;
	}

	if (dedup_mode) {
		flags |= MSYS_F_DEDUP;
	}

	if (streaming_mode) {
		flags |= MSYS_F_STREAMING;
	}

	struct collector c;
	memset(&c, 0, sizeof(c));
	collector_walk(&c, input, "");

	if (c.count == 0) {
		fprintf(stderr, "No files found under %s\n", input);
		return 1;
	}

	if (arch) add_metadata_entry(&c, "@meuos_arch", arch);

	/* Incremental merge: for unchanged files, reuse data from old archive */
	if (incremental) {
		struct msys *old = msys_open(output);
		if (!old) {
			fprintf(stderr, "mkmsys: --incremental but old %s not found, "
			        "doing full repack\n", output);
		} else {
			size_t reused = 0, total = 0;
			for (size_t i = 0; i < c.count; i++) {
				struct entry *e = &c.entries[i];
				if (e->name[0] == '@') continue; /* skip metadata entries */
				total++;
				/* Look up @mt/<name> in old archive */
				size_t mtname_len = e->name_len + 4;
				char *mtname = malloc(mtname_len + 1);
				memcpy(mtname, "@mt/", 4);
				memcpy(mtname + 4, e->name, e->name_len);
				mtname[mtname_len] = '\0';
				size_t old_mt_size;
				const void *old_mt = msys_search(old, mtname, &old_mt_size);
				free(mtname);
				if (old_mt && old_mt_size == 8) {
					uint64_t old_mt_val = 0;
					for (int j = 0; j < 8; j++)
						old_mt_val |= ((uint64_t)((const uint8_t*)old_mt)[j]) << (8*j);
					if (old_mt_val == (uint64_t)e->mtime) {
						/* Unchanged — read data from old archive */
						void *old_data = NULL;
						size_t old_size;
						if (msys_load(old, e->name, &old_data, &old_size) >= 0) {
							free(e->data);
							e->data = old_data; /* msys_load returns allocated buffer */
							e->data_size = old_size;
							reused++;
						}
					}
				}
			}
			if (total > 0)
				fprintf(stderr, "mkmsys: incremental — reused %zu/%zu unchanged files\n",
				        reused, total);
			msys_close(old);
		}
	}

	qsort(c.entries, c.count, sizeof(struct entry), entry_cmp);

	/* Read ed25519 secret key for signing (if --sign) */
	uint8_t sign_sk[64] = {0};
	uint8_t sign_pk[32] = {0};
	const uint8_t *sign_sk_ptr = NULL;
	const uint8_t *sign_pk_ptr = NULL;
	if (sign_keyfile) {
		FILE *kf = fopen(sign_keyfile, "rb");
		if (!kf) { perror(sign_keyfile); return 1; }
		uint8_t key_buf[64];
		size_t klen = fread(key_buf, 1, 64, kf);
		fclose(kf);
		if (klen == 32) {
			/* Seed: generate key pair */
			ed25519_keypair(key_buf, sign_sk, sign_pk);
			sign_sk_ptr = sign_sk;
			sign_pk_ptr = sign_pk;
		} else if (klen == 64) {
			/* Full secret key */
			memcpy(sign_sk, key_buf, 64);
			memcpy(sign_pk, key_buf + 32, 32);
			sign_sk_ptr = sign_sk;
			sign_pk_ptr = sign_pk;
		} else {
			fprintf(stderr, "mkmsys: invalid key file '%s' — expected 32 (seed) or 64 bytes\n",
			        sign_keyfile);
			return 1;
		}
	}

	if (format_v2)
		write_msys_v2(output, &c, flags, NULL, 0, sign_sk_ptr, sign_pk_ptr);
	else
		write_msys(output, &c, flags);

	printf("Wrote %zu entries to %s\n", c.count, output);
	collector_free(&c);
	return 0;
}
