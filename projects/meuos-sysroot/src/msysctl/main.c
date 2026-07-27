/* msysctl — Unified CLI for .msys archives.
 *
 * Usage:
 *   msysctl cat <archive> <path>    — print file to stdout
 *   msysctl ls <archive> [path]     — list directory
 *   msysctl find <archive> [path]   — recursive find
 *   msysctl tree <archive>          — directory tree
 *   msysctl extract <archive> [dir] — extract to directory
 *   msysctl verify <archive>        — verify all SHA-256
 *   msysctl stat <archive> <path>   — print file metadata
 */

#include "mt/msys.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void die(const char *msg) { perror(msg); exit(1); }
static void usage(void) {
	fprintf(stderr, "Usage: msysctl <cat|ls|find|tree|extract|verify|stat> <archive> [args...]\n");
	exit(1);
}

/* ---- cat ---- */
static int cmd_cat(struct msys *m, const char *path) {
	void *buf; size_t sz;
	if (msys_load(m, path, &buf, &sz) < 0) return -1;
	fwrite(buf, 1, sz, stdout);
	free(buf); return 0;
}

/* ---- ls ---- */
static int ls_cb(const char *name, size_t nlen, size_t size, int is_dir, void *arg) {
	(void)size; (void)arg;
	printf("%s%.*s\n", is_dir ? "d " : "  ", (int)nlen, name);
	return 0;
}
static int cmd_ls(struct msys *m, const char *dir) {
	return msys_readdir(m, dir ? dir : "", ls_cb, NULL);
}

/* ---- find (recursive) ---- */
static void find_dir(struct msys *m, const char *dir) {
	char **names = NULL;
	size_t cnt = 0, cap = 0;
	uint32_t total = msys_count(m);
	size_t dlen = strlen(dir);

	for (uint32_t i = 0; i < total; i++) {
		const char *ename; size_t elen, esize;
		if (msys_enumerate(m, i, &ename, &elen, &esize) < 0) continue;
		if (elen > 0 && ename[0] == '@') continue;
		const char *child = ename; size_t child_len = elen;
		if (dlen > 0) {
			if (elen <= dlen || memcmp(ename, dir, dlen) != 0 || ename[dlen] != '/') continue;
			child = ename + dlen + 1; child_len = elen - dlen - 1;
		}
		const char *slash = memchr(child, '/', child_len);
		if (slash) child_len = (size_t)(slash - child);
		if (child_len == 0) continue;
		int dup = 0;
		for (size_t j = 0; j < cnt; j++)
			if (strlen(names[j]) == child_len && memcmp(names[j], child, child_len) == 0) { dup = 1; break; }
		if (dup) continue;
		char *copy = strndup(child, child_len);
		if (!copy) goto cleanup;
		if (cnt >= cap) {
			cap = cap ? cap * 2 : 64;
			char **nn = realloc(names, cap * sizeof(char *));
			if (!nn) goto cleanup;
			names = nn;
		}
		names[cnt++] = copy;
	}
	for (size_t i = 0; i < cnt; i++) {
		int is_dir = 0;
		size_t ndir = dlen + 1 + strlen(names[i]);
		for (uint32_t j = 0; j < total; j++) {
			const char *ename; size_t elen, esize;
			if (msys_enumerate(m, j, &ename, &elen, &esize) < 0) continue;
			if (elen > ndir + 1 && memcmp(ename, dir, dlen) == 0 && ename[dlen] == '/' &&
			    memcmp(ename + dlen + 1, names[i], strlen(names[i])) == 0 &&
			    ename[dlen + 1 + strlen(names[i])] == '/') { is_dir = 1; break; }
		}
		char full[4096];
		snprintf(full, sizeof(full), "%s%s%s", dir, dlen > 0 ? "/" : "", names[i]);
		printf("%s\n", full);
		if (is_dir) find_dir(m, full);
	}
cleanup:
	for (size_t i = 0; i < cnt; i++) free(names[i]);
	free(names);
}
static int cmd_find(struct msys *m, const char *dir) { find_dir(m, dir ? dir : ""); return 0; }

/* ---- tree ---- */
static void tree_dir(struct msys *m, const char *dir, int depth) {
	char **names = NULL; size_t cnt = 0, cap = 0;
	uint32_t total = msys_count(m); size_t dlen = strlen(dir);

	for (uint32_t i = 0; i < total; i++) {
		const char *ename; size_t elen, esize;
		if (msys_enumerate(m, i, &ename, &elen, &esize) < 0) continue;
		if (elen > 0 && ename[0] == '@') continue;
		const char *child = ename; size_t child_len = elen;
		if (dlen > 0) {
			if (elen <= dlen || memcmp(ename, dir, dlen) != 0 || ename[dlen] != '/') continue;
			child = ename + dlen + 1; child_len = elen - dlen - 1;
		}
		const char *slash = memchr(child, '/', child_len);
		if (slash) child_len = (size_t)(slash - child);
		if (child_len == 0) continue;
		int dup = 0;
		for (size_t j = 0; j < cnt; j++)
			if (strlen(names[j]) == child_len && memcmp(names[j], child, child_len) == 0) { dup = 1; break; }
		if (dup) continue;
		char *copy = strndup(child, child_len);
		if (!copy) goto cleanup_tree;
		if (cnt >= cap) {
			cap = cap ? cap * 2 : 64;
			char **nn = realloc(names, cap * sizeof(char *));
			if (!nn) goto cleanup_tree;
			names = nn;
		}
		names[cnt++] = copy;
	}
	for (size_t i = 0; i < cnt; i++) {
		int is_dir = 0;
		size_t ndir = dlen + 1 + strlen(names[i]);
		for (uint32_t j = 0; j < total; j++) {
			const char *ename; size_t elen, esize;
			if (msys_enumerate(m, j, &ename, &elen, &esize) < 0) continue;
			if (elen > ndir + 1 && memcmp(ename, dir, dlen) == 0 && ename[dlen] == '/' &&
			    memcmp(ename + dlen + 1, names[i], strlen(names[i])) == 0 &&
			    ename[dlen + 1 + strlen(names[i])] == '/') { is_dir = 1; break; }
		}
		printf("%*s%s%s\n", depth * 2, "", i == cnt - 1 ? "└── " : "├── ", names[i]);
		if (is_dir) {
			char sub[4096]; snprintf(sub, sizeof(sub), "%s%s%s", dir, dlen > 0 ? "/" : "", names[i]);
			tree_dir(m, sub, depth + 1);
		}
	}
cleanup_tree:
	for (size_t i = 0; i < cnt; i++) free(names[i]);
	free(names);
}
static int cmd_tree(struct msys *m) { printf(".msys: %u entries\n", msys_count(m)); tree_dir(m, "", 0); return 0; }

/* ---- extract ---- */
static int cmd_extract(struct msys *m, const char *outdir) {
	uint32_t cnt = msys_count(m); size_t extracted = 0;
	for (uint32_t i = 0; i < cnt; i++) {
		const char *name; size_t nlen, dsize;
		if (msys_enumerate(m, i, &name, &nlen, &dsize) < 0) continue;
		if (nlen > 0 && name[0] == '@') continue;
		char path[8192]; char namebuf[4096];
		if (nlen >= sizeof(namebuf)) continue;
		memcpy(namebuf, name, nlen); namebuf[nlen] = '\0';
		int n = snprintf(path, sizeof(path), "%s/%s", outdir, namebuf);
		if (n < 0 || (size_t)n >= sizeof(path)) continue;
		char *slash = strrchr(path, '/');
		if (slash && slash != path) { *slash = '\0'; mkdir(path, 0755); *slash = '/'; }
		if (dsize > 0) {
			void *buf; if (msys_load(m, namebuf, &buf, NULL) < 0) continue;
			FILE *fp = fopen(path, "wb"); if (fp) { fwrite(buf, 1, dsize, fp); fclose(fp); } free(buf);
		} else { FILE *fp = fopen(path, "wb"); if (fp) fclose(fp); }
		extracted++;
	}
	printf("Extracted %zu entries to %s\n", extracted, outdir);
	return 0;
}

/* ---- verify ---- */
static int cmd_verify(struct msys *m) {
	int ret = msys_verify_all(m);
	printf(ret == 0 ? "All OK\n" : "FAILED\n");
	return ret;
}

/* ---- stat ---- */
static int cmd_stat(struct msys *m, const char *path) {
	struct msys_stat st;
	if (msys_stat(m, path, &st) < 0) { perror("stat"); return -1; }
	static const char *ft[] = {"REG","DIR","SYMLINK","CHR","BLK","FIFO","SOCK"};
	printf("  %s  size=%zu  mode=0%03o  uid=%u  gid=%u\n",
	       st.file_type < 7 ? ft[st.file_type] : "???", (size_t)st.size,
	       (unsigned)st.mode, st.uid, st.gid);
	char xbuf[256];
	if (msys_getxattr(m, path, "user.mime_type", xbuf, sizeof(xbuf)) > 0)
		printf("  mime: %s\n", xbuf);
	return 0;
}

/* ---- main ---- */
int main(int argc, char *argv[]) {
	if (argc < 3) usage();
	const char *cmd = argv[1], *archive = argv[2];
	struct msys *m = msys_open(archive);
	if (!m) die(archive);
	int ret = 0;
	if (strcmp(cmd, "cat") == 0) ret = argc < 4 ? (usage(), 1) : cmd_cat(m, argv[3]);
	else if (strcmp(cmd, "ls") == 0) ret = cmd_ls(m, argc > 3 ? argv[3] : "");
	else if (strcmp(cmd, "find") == 0) ret = cmd_find(m, argc > 3 ? argv[3] : "");
	else if (strcmp(cmd, "tree") == 0) ret = cmd_tree(m);
	else if (strcmp(cmd, "extract") == 0) ret = cmd_extract(m, argc > 3 ? argv[3] : ".");
	else if (strcmp(cmd, "verify") == 0) ret = cmd_verify(m);
	else if (strcmp(cmd, "stat") == 0) ret = argc < 4 ? (usage(), 1) : cmd_stat(m, argv[3]);
	else { fprintf(stderr, "Unknown: %s\n", cmd); usage(); }
	if (ret < 0) perror(cmd);
	msys_close(m);
	return ret < 0 ? 1 : 0;
}
