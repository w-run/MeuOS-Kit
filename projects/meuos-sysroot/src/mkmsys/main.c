/*
 * mkmsys — .msys single-file sysroot packer
 *
 * Usage:
 *   mkmsys -o <output.msys> <root-directory>
 *   mkmsys --list <input.msys>
 *   mkmsys -o <output.msys> --arch <arch> <root-directory>
 */

#include "mt/msys.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- LE read/write helpers ---- */

static uint64_t rd48(const uint8_t *buf)
{
	return (uint64_t)buf[0] | ((uint64_t)buf[1] << 8)
	     | ((uint64_t)buf[2] << 16) | ((uint64_t)buf[3] << 24)
	     | ((uint64_t)buf[4] << 32) | ((uint64_t)buf[5] << 40);
}
static uint32_t rd32(const uint8_t *buf) { return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24); }
static uint16_t rd16(const uint8_t *buf) { return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8); }
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
};

struct collector {
	struct entry *entries;
	size_t        count;
	size_t        cap;
};

static void collector_add(struct collector *c, const char *relpath,
                          const void *data, size_t data_size)
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
		if (stat(abspath, &st) < 0) { free(abspath); free(relpath); continue; }

		if (S_ISREG(st.st_mode)) {
			FILE *fp = fopen(abspath, "rb");
			if (!fp) { free(abspath); free(relpath); continue; }
			void *buf = malloc(st.st_size ? (size_t)st.st_size : 1);
			size_t nread = 0;
			if (st.st_size > 0)
				nread = fread(buf, 1, (size_t)st.st_size, fp);
			fclose(fp);
			collector_add(c, relpath, buf, nread);
			free(buf);
		} else if (S_ISDIR(st.st_mode)) {
			collector_walk(c, abspath, relpath);
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

static void write_msys(const char *output, struct collector *c)
{
	FILE *fp = fopen(output, "wb");
	if (!fp) die(output);

	/* Layout:
	 *   [Header (32 bytes)]
	 *   [Data block 1]  (4-byte aligned)
	 *   [Data block 2]
	 *   ...
	 *   [Data block N]
	 *   [Index block]   (begins at index_offset)
	 *
	 * We pre-compute all offsets then write sequentially.
	 */

	/* Phase 1: compute offsets */
	size_t hdr_size = sizeof(struct msys_header);
	size_t cur = hdr_size;
	size_t *data_offsets = malloc(c->count * sizeof(size_t));
	if (!data_offsets) die("malloc");

	for (size_t i = 0; i < c->count; i++) {
		cur = align4(cur);
		data_offsets[i] = cur;
		cur += c->entries[i].data_size;
	}
	uint64_t index_offset = align4(cur);

	/* Phase 2: write header */
	struct msys_header hdr;
	memset(&hdr, 0, sizeof(hdr));
	memcpy(hdr.magic, MSYS_MAGIC, MSYS_MAGIC_LEN);
	hdr.index_offset = index_offset;
	hdr.index_count  = (uint32_t)c->count;
	fwrite(&hdr, sizeof(hdr), 1, fp);

	/* Phase 3: write data blocks */
	for (size_t i = 0; i < c->count; i++) {
		struct entry *e = &c->entries[i];
		/* Pad to 4-byte alignment */
		long pos = ftell(fp);
		while ((size_t)pos < align4((size_t)pos)) {
			fputc(0, fp); pos++;
		}
		if (e->data_size > 0)
			fwrite(e->data, 1, e->data_size, fp);
	}

	/* Pad index to alignment */
	{
		long pos = ftell(fp);
		while ((size_t)pos < align4((size_t)pos)) { fputc(0, fp); pos++; }
	}

	/* Phase 4: write index entries (sorted by hash, already sorted) */
	for (size_t i = 0; i < c->count; i++) {
		struct entry *e = &c->entries[i];
		uint8_t buf[16];
		wr32(buf,     e->hash);
		wr48(buf + 4, data_offsets[i]);
		wr32(buf + 10, (uint32_t)e->data_size);
		wr16(buf + 14, (uint16_t)e->name_len);
		fwrite(buf, 16, 1, fp);
		fwrite(e->name, 1, e->name_len, fp);
	}

	free(data_offsets);
	fclose(fp);
}

/* ---- list ---- */

static int list_msys(const char *path)
{
	struct msys *m = msys_open(path);
	if (!m) die(path);

	printf(".msys file: %s\n", path);
	printf("Entries:    %u\n", m->hdr->index_count);
	printf("Index off:  %lu\n", (unsigned long)m->hdr->index_offset);
	printf("Flags:      0x%08x\n", m->hdr->flags);
	printf("---\n");

	uint32_t count = m->hdr->index_count;
	for (uint32_t i = 0; i < count; i++) {
		unsigned char *p = m->entries[i];
		uint32_t hash  = rd32(p);
		uint64_t off   = rd48(p + 4);
		uint32_t sz    = rd32(p + 10);
		uint16_t nlen  = rd16(p + 14);
		const char *name = (const char *)(p + 16);
		printf("  %08x  %8lu  %8u  %.*s\n",
		       hash, (unsigned long)off, sz, (int)nlen, name);
	}

	msys_close(m);
	return 0;
}

/* ---- add metadata entry ---- */

static void add_metadata_entry(struct collector *c, const char *key,
                               const char *value)
{
	collector_add(c, key, value, strlen(value));
}

/* ---- main ---- */

int main(int argc, char *argv[])
{
	const char *output = NULL;
	const char *arch = NULL;
	int list_mode = 0;
	const char *input = NULL;

	int i = 1;
	while (i < argc) {
		if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
			output = argv[i + 1]; i += 2;
		} else if (strcmp(argv[i], "--list") == 0) {
			list_mode = 1; i++;
		} else if (strcmp(argv[i], "--arch") == 0 && i + 1 < argc) {
			arch = argv[i + 1]; i += 2;
		} else if (input == NULL) {
			input = argv[i]; i++;
		} else {
			fprintf(stderr,
			  "Usage: mkmsys -o <output> [--arch <name>] <root-dir>\n"
			  "       mkmsys --list <input.msys>\n");
			return 1;
		}
	}

	if (list_mode) {
		if (!input) input = output;
		if (!input) { fprintf(stderr, "Usage: mkmsys --list <input.msys>\n"); return 1; }
		return list_msys(input);
	}

	if (!output || !input) {
		fprintf(stderr,
		  "Usage: mkmsys -o <output> [--arch <name>] <root-dir>\n");
		return 1;
	}

	struct collector c;
	memset(&c, 0, sizeof(c));
	collector_walk(&c, input, "");

	if (c.count == 0) {
		fprintf(stderr, "No files found under %s\n", input);
		return 1;
	}

	if (arch) add_metadata_entry(&c, "@meuos_arch", arch);

	qsort(c.entries, c.count, sizeof(struct entry), entry_cmp);
	write_msys(output, &c);

	printf("Wrote %zu entries to %s\n", c.count, output);
	collector_free(&c);
	return 0;
}
