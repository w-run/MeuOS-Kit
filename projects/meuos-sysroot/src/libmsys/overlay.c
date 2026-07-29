/* overlay.c — .msys overlay / layering support
 *
 * Allows stacking multiple .msys archives so that higher layers shadow
 * lower layers on search, while readdir merges all layers.
 */

#include "mt/msys.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- internal helpers ---- */

struct msys_overlay *
msys_overlay_open(const char **paths, int count)
{
	if (!paths || count < 1) { errno = EINVAL; return NULL; }

	struct msys_overlay *ol = calloc(1, sizeof(*ol));
	if (!ol) return NULL;

	ol->layers = calloc((size_t)count, sizeof(struct msys *));
	if (!ol->layers) { free(ol); return NULL; }

	for (int i = 0; i < count; i++) {
		ol->layers[i] = msys_open(paths[i]);
		if (!ol->layers[i]) {
			/* Close any already-opened layers */
			for (int j = 0; j < i; j++)
				msys_close(ol->layers[j]);
			free(ol->layers);
			free(ol);
			return NULL;
		}
		ol->count++;
	}
	ol->cap = count;
	return ol;
}

int
msys_overlay_add(struct msys_overlay *ol, const char *path)
{
	if (!ol || !path) { errno = EINVAL; return -1; }

	if (ol->count >= ol->cap) {
		int newcap = ol->cap ? ol->cap * 2 : 4;
		struct msys **nl = realloc(ol->layers,
		                           (size_t)newcap * sizeof(struct msys *));
		if (!nl) return -1;
		ol->layers = nl;
		ol->cap = newcap;
	}

	struct msys *m = msys_open(path);
	if (!m) return -1;

	ol->layers[ol->count++] = m;
	return 0;
}

int
msys_overlay_count(struct msys_overlay *ol)
{
	return ol ? ol->count : 0;
}

struct msys *
msys_overlay_get(struct msys_overlay *ol, int idx)
{
	if (!ol || idx < 0 || idx >= ol->count) { errno = EINVAL; return NULL; }
	return ol->layers[idx];
}

void
msys_overlay_close(struct msys_overlay *ol)
{
	if (!ol) return;
	for (int i = 0; i < ol->count; i++)
		msys_close(ol->layers[i]);
	free(ol->layers);
	free(ol);
}

/* ---- search / read ---- */

const void *
msys_overlay_search(struct msys_overlay *ol, const char *name,
                    size_t *size, int *layer)
{
	if (!ol || !name) { errno = EINVAL; return NULL; }

	/* Search from top (highest priority) down to base */
	for (int i = ol->count - 1; i >= 0; i--) {
		const void *data = msys_search(ol->layers[i], name, size);
		if (data) {
			if (layer) *layer = i;
			return data;
		}
		/* ENOENT means not in this layer — continue */
		if (errno != ENOENT && errno != ENOMSG)
			return NULL;
	}
	errno = ENOENT;
	return NULL;
}

int
msys_overlay_read(struct msys_overlay *ol, const char *name,
                  void *buf, size_t buflen)
{
	size_t dsize;
	const void *data = msys_overlay_search(ol, name, &dsize, NULL);
	if (!data) return -1;

	if (buflen > dsize) buflen = dsize;
	memcpy(buf, data, buflen);
	return (int)buflen;
}

FILE *
msys_overlay_fopen(struct msys_overlay *ol, const char *path,
                   const char *mode)
{
	if (!ol || !path) { errno = EINVAL; return NULL; }
	for (int i = ol->count - 1; i >= 0; i--) {
		FILE *fp = msys_fopen(ol->layers[i], path, mode);
		if (fp) return fp;
		if (errno != ENOENT) return NULL;
	}
	return NULL;
}

int
msys_overlay_load(struct msys_overlay *ol, const char *path,
                  void **buf, size_t *size)
{
	if (!ol || !path || !buf) { errno = EINVAL; return -1; }
	for (int i = ol->count - 1; i >= 0; i--) {
		int ret = msys_load(ol->layers[i], path, buf, size);
		if (ret >= 0) return ret;
		if (errno != ENOENT) return -1;
	}
	return -1;
}

int
msys_overlay_stat(struct msys_overlay *ol, const char *name,
                  struct msys_stat *st)
{
	if (!ol || !name) { errno = EINVAL; return -1; }
	for (int i = ol->count - 1; i >= 0; i--) {
		if (msys_stat(ol->layers[i], name, st) == 0)
			return 0;
		if (errno != ENOENT && errno != ENOMSG)
			return -1;
	}
	return -1;
}

int
msys_overlay_readlink(struct msys_overlay *ol, const char *name,
                      char *buf, size_t bufsize)
{
	if (!ol || !name) { errno = EINVAL; return -1; }
	for (int i = ol->count - 1; i >= 0; i--) {
		int ret = msys_readlink(ol->layers[i], name, buf, bufsize);
		if (ret >= 0) return ret;
		if (errno != ENOENT) return -1;
	}
	return -1;
}

/* ---- overlay readdir (merged, deduped) ---- */

/* The multi-pass overlay readdir:
 * 1. Collect all unique children from all layers (top-first so higher layers
 *    take priority when we de-dup).
 * 2. Invoke the real callback for each unique child.
 */
int
msys_overlay_readdir(struct msys_overlay *ol, const char *dir,
                     msys_dir_cb cb, void *arg)
{
	if (!ol || !dir || !cb) { errno = EINVAL; return -1; }

	/* Phase 1: collect unique children across all layers.
	 * Go top-down so higher-layer entries shadow lower-layer ones. */
	const char **names = NULL;
	size_t name_count = 0, name_cap = 0;
	size_t *sizes = NULL;
	int *is_dirs = NULL;

	for (int layer = ol->count - 1; layer >= 0; layer--) {
		if (ol->count == 0) continue;
		/* First pass: count unique children in this layer */
		/* We use a manual approach: scan all entries to collect children,
		 * then de-dup against the global set. */
		struct msys *m = ol->layers[layer];
		uint32_t cnt = msys_count(m);

		for (uint32_t i = 0; i < cnt; i++) {
			const char *ename;
			size_t elen, esize;
			if (msys_enumerate(m, i, &ename, &elen, &esize) < 0)
				continue;

			/* Skip @mt metadata entries */
			if (elen > 0 && ename[0] == '@') continue;

			/* Extract child relative to dir */
			const char *child = ename;
			size_t child_len = elen;
			size_t dlen = strlen(dir);

			if (dlen > 0) {
				if (elen <= dlen) continue;
				if (memcmp(ename, dir, dlen) != 0) continue;
				if (ename[dlen] != '/') continue;
				child = ename + dlen + 1;
				child_len = elen - dlen - 1;
			}

			/* Extract first path component */
			const char *slash = memchr(child, '/', child_len);
			if (slash) {
				child_len = (size_t)(slash - child);
			}
			if (child_len == 0) continue;

			/* Dedup against global names */
			int dup = 0;
			for (size_t j = 0; j < name_count; j++) {
				if (strlen(names[j]) == child_len &&
				    memcmp(names[j], child, child_len) == 0) {
					dup = 1; break;
				}
			}
			if (dup) continue;

			/* Add to global set */
			if (name_count >= name_cap) {
				size_t nc = name_cap ? name_cap * 2 : 64;

				const char **nn = realloc(names, nc * sizeof(const char *));
				if (!nn) goto oom;
				names = nn;

				size_t *ns = realloc(sizes, nc * sizeof(size_t));
				if (!ns) goto oom;
				sizes = ns;

				int *nd = realloc(is_dirs, nc * sizeof(int));
				if (!nd) goto oom;
				is_dirs = nd;

				name_cap = nc;
			}
			char *copy = malloc(child_len + 1);
			if (!copy) goto oom;
			memcpy(copy, child, child_len);
			copy[child_len] = '\0';
			names[name_count] = copy;
			sizes[name_count] = (slash == NULL) ? esize : 0;
			is_dirs[name_count] = (slash != NULL);
			name_count++;
		}
	}

	/* Phase 2: invoke callback for each unique child */
	for (size_t i = 0; i < name_count; i++) {
		int ret = cb(names[i], strlen(names[i]),
		             sizes[i], is_dirs[i], arg);
		if (ret) break;
	}

	/* Cleanup */
	for (size_t i = 0; i < name_count; i++)
		free((void *)names[i]);
	free(names);
	free(sizes);
	free(is_dirs);
	if (name_count == 0) { errno = ENOENT; return -1; }
	return 0;

oom:
	for (size_t i = 0; i < name_count; i++)
		free((void *)names[i]);
	free(names); free(sizes); free(is_dirs);
	errno = ENOMEM;
	return -1;
}

/* ---- verify ---- */

int
msys_overlay_verify(struct msys_overlay *ol, const char *name)
{
	if (!ol || !name) { errno = EINVAL; return -1; }
	for (int i = ol->count - 1; i >= 0; i--) {
		int ret = msys_verify(ol->layers[i], name);
		if (ret == 0) return 0; /* found and verified OK */
		/* If entry exists but hash mismatch, report error */
		if (errno != ENOENT && errno != ENOMSG)
			return -1;
	}
	errno = ENOENT;
	return -1;
}
