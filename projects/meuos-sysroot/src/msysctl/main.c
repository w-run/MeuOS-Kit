/* msysctl — Unified CLI for .msys archives.
 *
#pragma GCC diagnostic ignored "-Wformat-truncation"
 * Usage:
 *   msysctl <cmd> <archive> [args...]                        — single archive
 *   msysctl --overlay a.msys,b.msys <cmd> [args]             — overlay
 *   msysctl hist add <archive> <path> [msg]                  — save version
 *   msysctl hist list <archive> [path]                       — list versions
 *   msysctl hist cat <archive> <path> <rev>                  — show version
 *   msysctl hist diff <archive> <path> <rev1> <rev2>         — diff versions
 *
 * Commands: cat <path>, ls [dir], find [dir], tree, extract [dir],
 *           info, verify, stat <path>
 */

#include "mt/msys.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

/* Forward declaration for overlay wrapper functions */
struct archive;

static void die(const char *msg) { perror(msg); exit(1); }
static void usage(void) {
	fprintf(stderr, "Usage:\n"
		"  msysctl [--overlay a.msys,b.msys] <cmd> <archive> [args...]\n"
		"  msysctl hist <add|list|cat|diff> <archive> [args...]\n"
		"Commands: cat <path> | ls [dir] | find [dir] | tree |\n"
		"          extract [dir] | info | verify | stat <path>\n");
	exit(1);
}

/* ── helpers ── */
static const char *basename_of(const char *path) {
	const char *s = strrchr(path, '/');
	return s ? s + 1 : path;
}

/* ── ls callback ── */
static int ls_cb(const char *name, size_t nlen, size_t size, int is_dir, void *arg) {
	(void)size; (void)arg;
	printf("%s%.*s\n", is_dir ? "d " : "  ", (int)nlen, name);
	return 0;
}

/* ── find (recursive) ── */
static void find_dir(struct msys *m, const char *dir) {
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
		if (!copy) goto cleanup;
		if (cnt >= cap) { cap = cap ? cap * 2 : 64; char **nn = realloc(names, cap * sizeof(char *));
			if (!nn) { goto cleanup; } names = nn; }
		names[cnt++] = copy;
	}
	for (size_t i = 0; i < cnt; i++) {
		int is_dir = 0; size_t ndir = dlen + 1 + strlen(names[i]);
		for (uint32_t j = 0; j < total; j++) {
			const char *ename; size_t elen, esize;
			if (msys_enumerate(m, j, &ename, &elen, &esize) < 0) continue;
			if (elen > ndir + 1 && memcmp(ename, dir, dlen) == 0 && ename[dlen] == '/' &&
			    memcmp(ename + dlen + 1, names[i], strlen(names[i])) == 0 &&
			    ename[dlen + 1 + strlen(names[i])] == '/') { is_dir = 1; break; }
		}
		char full[4096]; snprintf(full, sizeof(full), "%s%s%s", dir, dlen > 0 ? "/" : "", names[i]);
		printf("%s\n", full);
		if (is_dir) find_dir(m, full);
	}
cleanup: for (size_t i = 0; i < cnt; i++) free(names[i]); free(names);
}
static int cmd_find(struct msys *m, const char *dir) { find_dir(m, dir ? dir : ""); return 0; }

/* ── tree ── */
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
		if (cnt >= cap) { cap = cap ? cap * 2 : 64; char **nn = realloc(names, cap * sizeof(char *));
			if (!nn) { goto cleanup_tree; } names = nn; }
		names[cnt++] = copy;
	}
	for (size_t i = 0; i < cnt; i++) {
		int is_dir = 0; size_t ndir = dlen + 1 + strlen(names[i]);
		for (uint32_t j = 0; j < total; j++) {
			const char *ename; size_t elen, esize;
			if (msys_enumerate(m, j, &ename, &elen, &esize) < 0) continue;
			if (elen > ndir + 1 && memcmp(ename, dir, dlen) == 0 && ename[dlen] == '/' &&
			    memcmp(ename + dlen + 1, names[i], strlen(names[i])) == 0 &&
			    ename[dlen + 1 + strlen(names[i])] == '/') { is_dir = 1; break; }
		}
		printf("%*s%s%s\n", depth * 2, "", i == cnt - 1 ? "└── " : "├── ", names[i]);
		if (is_dir) { char sub[4096]; snprintf(sub, sizeof(sub), "%s%s%s", dir, dlen > 0 ? "/" : "", names[i]);
			tree_dir(m, sub, depth + 1); }
	}
cleanup_tree: for (size_t i = 0; i < cnt; i++) free(names[i]); free(names);
}

/* ── stat ── */
static int cmd_stat(struct msys *m, const char *path) {
	struct msys_stat st;
	if (msys_stat(m, path, &st) < 0) { perror("stat"); return -1; }
	static const char *ft[] = {"REG","DIR","SYMLINK","CHR","BLK","FIFO","SOCK"};
	printf("  %s  size=%zu  mode=0%03o  uid=%u  gid=%u\n",
	       st.file_type < 7 ? ft[st.file_type] : "???", (size_t)st.size,
	       (unsigned)st.mode, st.uid, st.gid);
	char xbuf[256];
	if (msys_getxattr(m, path, "user.mime_type", xbuf, sizeof(xbuf)) > 0) printf("  mime: %s\n", xbuf);
	return 0;
}

/* ── extract (single archive) ── */
static int cmd_extract(struct msys *m, const char *outdir) {
	uint32_t cnt = msys_count(m); size_t extracted = 0;
	for (uint32_t i = 0; i < cnt; i++) {
		const char *name; size_t nlen, dsize;
		if (msys_enumerate(m, i, &name, &nlen, &dsize) < 0) continue;
		if (nlen > 0 && name[0] == '@') continue;
		char namebuf[4096]; if (nlen >= sizeof(namebuf)) continue;
		memcpy(namebuf, name, nlen); namebuf[nlen] = '\0';
		char path[16384]; int n = snprintf(path, sizeof(path), "%s/%s", outdir, namebuf);
		if (n < 0 || (size_t)n >= sizeof(path)) continue;
		char *slash = strrchr(path, '/');
		if (slash && slash != path) { *slash = '\0'; mkdir(path, 0755); *slash = '/'; }
		void *buf;
		if (msys_load(m, namebuf, &buf, NULL) >= 0) {
			FILE *fp = fopen(path, "wb"); if (fp) { fwrite(buf, 1, dsize, fp); fclose(fp); } free(buf);
		}
		extracted++;
	}
	printf("Extracted %zu entries to %s\n", extracted, outdir);
	return 0;
}

/* ── info (archive statistics) ── */

/* ── history ── */

/* History is stored on disk in .msys.hist/<basename>/ */

static const char *hist_dir(const char *archive) {
	static char buf[4096];
	const char *bn = basename_of(archive);
	snprintf(buf, sizeof(buf), ".msys.hist/%s", bn);
	return buf;
}

static int cmd_hist_add(const char *archive, const char *path, const char *msg) {
	struct msys *m = msys_open(archive);
	if (!m) die(archive);

	void *data; size_t dsize;
	if (msys_load(m, path, &data, &dsize) < 0) { perror("load"); msys_close(m); return -1; }

	/* Create history dir: .msys.hist/<basename>/<path>/ */
	char dir[16384];
	snprintf(dir, sizeof(dir), "%s/%s", hist_dir(archive), path);
	/* mkdir -p */
	char *p = dir;
	while ((p = strchr(p + 1, '/'))) { *p = '\0'; mkdir(dir, 0755); *p = '/'; }
	mkdir(dir, 0755);

	/* Save as <timestamp> */
	char file[16384];
	snprintf(file, sizeof(file), "%s/%lu", dir, (unsigned long)time(NULL));
	FILE *fp = fopen(file, "wb");
	if (!fp) { perror("fopen"); free(data); msys_close(m); return -1; }
	fwrite(data, 1, dsize, fp);
	fclose(fp);

	/* Save message if provided */
	if (msg && *msg) {
		char msgfile[16384];
		snprintf(msgfile, sizeof(msgfile), "%s/%lu.msg", dir, (unsigned long)time(NULL));
		fp = fopen(msgfile, "wb");
		if (fp) { fwrite(msg, 1, strlen(msg), fp); fclose(fp); }
	}

	printf("saved %s v%lu (%zu bytes)%s%s\n", path, (unsigned long)time(NULL), dsize,
	       msg ? " — " : "", msg ? msg : "");
	free(data);
	msys_close(m);
	return 0;
}

static int cmd_hist_list(const char *archive, const char *filter_path) {
	const char *hd = hist_dir(archive);

	if (filter_path) {
		/* List versions for specific file */
		char dir[16384];
		snprintf(dir, sizeof(dir), "%s/%s", hd, filter_path);
		DIR *d = opendir(dir);
		if (!d) { fprintf(stderr, "no history for %s\n", filter_path); return -1; }

		struct dirent *de;
		while ((de = readdir(d))) {
			if (de->d_name[0] == '.') continue;
			size_t nl = strlen(de->d_name);
			if (nl > 4 && strcmp(de->d_name + nl - 4, ".msg") == 0) continue;

			char path[16384];
			snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
			struct stat st;
			if (stat(path, &st) < 0) continue;

			/* Try to read message */
			char msgfile[16384];
			snprintf(msgfile, sizeof(msgfile), "%s/%s.msg", dir, de->d_name);
			char msg[256] = "";
			FILE *mf = fopen(msgfile, "rb");
			if (mf) {
				size_t r = fread(msg, 1, sizeof(msg) - 1, mf);
				msg[r] = '\0';
				fclose(mf);
			}

			printf("  %s  %zu bytes  %s%s\n", de->d_name, (size_t)st.st_size,
			       msg, msg[0] ? "" : "");
		}
		closedir(d);
		return 0;
	}

	/* List all tracked files */
	DIR *d = opendir(hd);
	if (!d) { fprintf(stderr, "no history for %s\n", basename_of(archive)); return -1; }
	struct dirent *de;
	while ((de = readdir(d))) {
		if (de->d_name[0] == '.') continue;
		/* Check if it's a directory (tracked file) */
		char sub[16384]; snprintf(sub, sizeof(sub), "%s/%s", hd, de->d_name);
		struct stat st;
		if (stat(sub, &st) == 0 && S_ISDIR(st.st_mode)) {
			/* Count versions */
			DIR *sd = opendir(sub);
			if (!sd) continue;
			int vc = 0;
			struct dirent *sde;
			while ((sde = readdir(sd))) {
				if (sde->d_name[0] != '.') vc++;
			}
			closedir(sd);
			printf("  %s  (%d versions)\n", de->d_name, vc);
		}
	}
	closedir(d);
	return 0;
}

static int cmd_hist_cat(const char *archive, const char *path, const char *rev) {
	char file[16384];
	snprintf(file, sizeof(file), "%s/%s/%s", hist_dir(archive), path, rev);
	FILE *fp = fopen(file, "rb");
	if (!fp) { perror("fopen"); return -1; }
	fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
	char *buf = malloc((size_t)sz ? (size_t)sz : 1);
	if (!buf) { fclose(fp); return -1; }
	fread(buf, 1, (size_t)sz, fp);
	fclose(fp);
	fwrite(buf, 1, (size_t)sz, stdout);
	free(buf);
	return 0;
}

static int cmd_hist_diff(const char *archive, const char *path, const char *rev1, const char *rev2) {
	char f1[16384], f2[8192];
	snprintf(f1, sizeof(f1), "%s/%s/%s", hist_dir(archive), path, rev1);
	snprintf(f2, sizeof(f2), "%s/%s/%s", hist_dir(archive), path, rev2);

	char cmd[16384];
	snprintf(cmd, sizeof(cmd), "diff -u %s %s 2>/dev/null || true", f1, f2);
	return system(cmd);
}

/* ── overlay wrappers ── */

struct archive {
	int is_overlay;
	struct msys *single;
	struct msys_overlay *overlay;
};

/* ── overlay dispatch ── (same as before) */
static struct archive *open_archive(const char *arg, const char *overlay_arg) {
	struct archive *a = calloc(1, sizeof(*a));
	if (!a) { errno = ENOMEM; return NULL; }
	if (overlay_arg) {
		int count = 1;
		for (const char *p = overlay_arg; *p; p++) if (*p == ',') count++;
		const char **paths = malloc((size_t)count * sizeof(char *));
		if (!paths) { free(a); return NULL; }
		char *copy = strdup(overlay_arg), *save = copy, *tok; int i = 0;
		while ((tok = strtok_r(save, ",", &save))) paths[i++] = tok;
		a->overlay = msys_overlay_open(paths, i);
		free(copy); free(paths);
		if (!a->overlay) { free(a); return NULL; }
		a->is_overlay = 1;
	} else {
		a->single = msys_open(arg);
		if (!a->single) { free(a); return NULL; }
		a->is_overlay = 0;
	}
	return a;
}
static void close_archive(struct archive *a) {
	if (!a) return;
	if (a->is_overlay) msys_overlay_close(a->overlay); else msys_close(a->single);
	free(a);
}
static int arch_ls(struct archive *a, const char *dir) {
	return a->is_overlay ? msys_overlay_readdir(a->overlay, dir ? dir : "", ls_cb, NULL)
	                     : msys_readdir(a->single, dir ? dir : "", ls_cb, NULL);
}
static int arch_load(struct archive *a, const char *path, void **buf, size_t *sz) {
	return a->is_overlay ? msys_overlay_load(a->overlay, path, buf, sz)
	                     : msys_load(a->single, path, buf, sz);
}
static int arch_verify_all(struct archive *a) {
	if (a->is_overlay) {
		uint32_t c = (uint32_t)msys_overlay_count(a->overlay);
		for (uint32_t i = 0; i < c; i++) {
			struct msys *m = msys_overlay_get(a->overlay, (int)i);
			uint32_t cnt = msys_count(m);
			for (uint32_t j = 0; j < cnt; j++) {
				const char *n; size_t nl, ds;
				if (msys_enumerate(m, j, &n, &nl, &ds) < 0) continue;
				if (nl > 0 && n[0] == '@') continue;
				char nb[4096]; if (nl >= sizeof(nb)) continue;
				memcpy(nb, n, nl); nb[nl] = '\0';
				if (msys_overlay_verify(a->overlay, nb) < 0) return -1;
			}
		}
		printf("All OK\n"); return 0;
	}
	int r = msys_verify_all(a->single);
	printf(r == 0 ? "All OK\n" : "FAILED\n"); return r;
}
static void arch_tree(struct archive *a) {
	if (a->is_overlay) {
		uint32_t c = (uint32_t)msys_overlay_count(a->overlay); int total = 0;
		for (uint32_t i = 0; i < c; i++) total += (int)msys_count(msys_overlay_get(a->overlay, (int)i));
		printf("overlay (%u layers): %d entries\n", c, total);
	} else printf(".msys: %u entries\n", msys_count(a->single));
	tree_dir(a->is_overlay ? msys_overlay_get(a->overlay, 0) : a->single, "", 0);
}
static int arch_stat(struct archive *a, const char *path) {
	if (a->is_overlay) {
		struct msys_stat st;
		if (msys_overlay_stat(a->overlay, path, &st) < 0) return -1;
		static const char *ft[] = {"REG","DIR","SYMLINK","CHR","BLK","FIFO","SOCK"};
		printf("  %s  size=%zu  mode=0%03o  uid=%u  gid=%u\n",
		       st.file_type < 7 ? ft[st.file_type] : "???", (size_t)st.size,
		       (unsigned)st.mode, st.uid, st.gid);
		return 0;
	}
	return cmd_stat(a->single, path);
}
static int arch_extract(struct archive *a, const char *dir) {
	if (!a->is_overlay) return cmd_extract(a->single, dir);
	uint32_t c = (uint32_t)msys_overlay_count(a->overlay); size_t extracted = 0;
	for (uint32_t layer = c; layer > 0; layer--) {
		struct msys *m = msys_overlay_get(a->overlay, (int)(layer - 1));
		uint32_t cnt = msys_count(m);
		for (uint32_t i = 0; i < cnt; i++) {
			const char *name; size_t nlen, dsize;
			if (msys_enumerate(m, i, &name, &nlen, &dsize) < 0) continue;
			if (nlen > 0 && name[0] == '@') continue;
			char namebuf[4096]; if (nlen >= sizeof(namebuf)) continue;
			memcpy(namebuf, name, nlen); namebuf[nlen] = '\0';
			char path[16384]; snprintf(path, sizeof(path), "%s/%s", dir, namebuf);
			if (access(path, F_OK) == 0) continue;
			char *slash = strrchr(path, '/');
			if (slash && slash != path) { *slash = '\0'; mkdir(path, 0755); *slash = '/'; }
			void *buf;
			if (msys_load(m, namebuf, &buf, NULL) >= 0) {
				FILE *fp = fopen(path, "wb"); if (fp) { fwrite(buf, 1, dsize, fp); fclose(fp); } free(buf);
			}
			extracted++;
		}
	}
	printf("Extracted %zu entries to %s\n", extracted, dir);
	return 0;
}

/* ── main ── */
static int cmd_info(struct archive *a);
int main(int argc, char *argv[]) {
	if (argc < 2) usage();

	/* hist subcommand */
	if (strcmp(argv[1], "hist") == 0) {
		if (argc < 4) { fprintf(stderr, "Usage: msysctl hist <add|list|cat|diff> <archive> [args...]\n"); return 1; }
		const char *sub = argv[2];
		const char *archive = argv[3];

		if (strcmp(sub, "add") == 0) {
			if (argc < 5) { fprintf(stderr, "Usage: msysctl hist add <archive> <path> [msg]\n"); return 1; }
			return cmd_hist_add(archive, argv[4], argc > 5 ? argv[5] : NULL);
		}
		if (strcmp(sub, "list") == 0)
			return cmd_hist_list(archive, argc > 4 ? argv[4] : NULL);
		if (strcmp(sub, "cat") == 0) {
			if (argc < 6) { fprintf(stderr, "Usage: msysctl hist cat <archive> <path> <rev>\n"); return 1; }
			return cmd_hist_cat(archive, argv[4], argv[5]);
		}
		if (strcmp(sub, "diff") == 0) {
			if (argc < 7) { fprintf(stderr, "Usage: msysctl hist diff <archive> <path> <rev1> <rev2>\n"); return 1; }
			return cmd_hist_diff(archive, argv[4], argv[5], argv[6]);
		}
		fprintf(stderr, "Unknown hist subcommand: %s\n", sub);
		return 1;
	}

	if (argc < 3) usage();

	const char *overlay_arg = NULL;
	const char *cmd, *archive;
	int path_idx;

	if (strcmp(argv[1], "--overlay") == 0) {
		if (argc < 4) usage();
		overlay_arg = argv[2];
		cmd = argv[3];
		archive = overlay_arg;
		path_idx = 4;
	} else {
		cmd = argv[1];
		archive = argv[2];
		path_idx = 3;
	}

	struct archive *a = open_archive(archive, overlay_arg);
	if (!a) die(overlay_arg ? overlay_arg : archive);

	int ret = 0;
	if (strcmp(cmd, "cat") == 0) {
		if (path_idx >= argc) { usage(); ret = 1; }
		else { void *b; size_t s;
			ret = arch_load(a, argv[path_idx], &b, &s);
			if (ret >= 0) { fwrite(b, 1, s, stdout); free(b); } }
	} else if (strcmp(cmd, "ls") == 0) ret = arch_ls(a, path_idx < argc ? argv[path_idx] : "");
	else if (strcmp(cmd, "find") == 0) ret = cmd_find(
		a->is_overlay ? msys_overlay_get(a->overlay, 0) : a->single,
		path_idx < argc ? argv[path_idx] : "");
	else if (strcmp(cmd, "tree") == 0) arch_tree(a);
	else if (strcmp(cmd, "extract") == 0) ret = arch_extract(a, path_idx < argc ? argv[path_idx] : ".");
	else if (strcmp(cmd, "info") == 0) ret = cmd_info(a);
	else if (strcmp(cmd, "verify") == 0) ret = arch_verify_all(a);
	else if (strcmp(cmd, "stat") == 0) ret = path_idx >= argc ? (usage(), 1) : arch_stat(a, argv[path_idx]);
	else if (strcmp(cmd, "grep") == 0) {
		if (path_idx >= argc) { usage(); ret = 1; }
		else { const char *pat = argv[path_idx]; int count = 0;
			if (a->is_overlay) {
				/* Overlay: for each unique visible file, search the matching layer */
				/* First collect all files from all layers (top-first) */
				int nlayers = msys_overlay_count(a->overlay);
				for (int li = nlayers - 1; li >= 0; li--) {
					struct msys *m = msys_overlay_get(a->overlay, li);
					uint32_t cnt = msys_count(m);
					for (uint32_t i = 0; i < cnt; i++) {
						const char *name; size_t nlen, dsize;
						if (msys_enumerate(m, i, &name, &nlen, &dsize) < 0) continue;
						if (nlen > 0 && name[0] == '@') continue;
						if (dsize == 0) continue;
						char namebuf[4096]; if (nlen >= sizeof(namebuf)) continue;
						memcpy(namebuf, name, nlen); namebuf[nlen] = '\0';
						/* Check if a higher layer shadows this file */
						int shadowed = 0;
						for (int hl = nlayers - 1; hl > li; hl--) {
							if (msys_search(msys_overlay_get(a->overlay, hl), namebuf, NULL)) {
								shadowed = 1; break;
							}
						}
						if (shadowed) continue;
						/* Check if already reported from a lower layer */
						/* Load and search */
						void *data;
						if (arch_load(a, namebuf, &data, NULL) < 0) continue;
						const unsigned char *p = (const unsigned char *)data;
						size_t plen = strlen(pat);
						int found = 0;
						for (size_t j = 0; j + plen <= dsize; j++)
							if (memcmp(p + j, pat, plen) == 0) { found = 1; break; }
						if (found) { printf("%s\n", namebuf); count++; }
						free(data);
					}
				}
			} else {
				struct msys *m = a->single;
				uint32_t cnt = msys_count(m);
				for (uint32_t i = 0; i < cnt; i++) {
					const char *name; size_t nlen, dsize;
					if (msys_enumerate(m, i, &name, &nlen, &dsize) < 0) continue;
					if (nlen > 0 && name[0] == '@') continue;
					if (dsize == 0) continue;
					char namebuf[4096]; if (nlen >= sizeof(namebuf)) continue;
					memcpy(namebuf, name, nlen); namebuf[nlen] = '\0';
					void *data;
					if (arch_load(a, namebuf, &data, NULL) < 0) continue;
					const unsigned char *p = (const unsigned char *)data;
					size_t plen = strlen(pat);
					int found = 0;
					for (size_t j = 0; j + plen <= dsize; j++)
						if (memcmp(p + j, pat, plen) == 0) { found = 1; break; }
					if (found) { printf("%s\n", namebuf); count++; }
					free(data);
				}
			}
			if (count > 0) printf("-- %d match%s\n", count, count == 1 ? "" : "es");
			if (count == 0) { ret = -1; errno = ENOENT; }
		}
	}
	/* ── diff: compare archive file with local file ── */
	else if (strcmp(cmd, "diff") == 0) {
		if (path_idx >= argc) { usage(); ret = 1; }
		else {
			const char *apath = argv[path_idx]; /* path within archive */
			const char *lfile = path_idx + 1 < argc ? argv[path_idx + 1] : NULL;
			/* Extract to temp file */
			void *data; size_t dsize;
			if (arch_load(a, apath, &data, &dsize) < 0) { ret = -1; }
			char tmpfile[64]; snprintf(tmpfile, sizeof(tmpfile), "/tmp/msys-diff-XXXXXX");
			int fd = mkstemp(tmpfile);
			if (fd < 0) { free(data); ret = -1; }
			write(fd, data, dsize); close(fd);
			if (lfile) {
				char cmdline[8192];
				snprintf(cmdline, sizeof(cmdline), "diff -u %s %s 2>/dev/null || true", lfile, tmpfile);
				ret = system(cmdline);
			} else {
				printf("Archive: %s (%zu bytes)\n", apath, dsize);
			}
			unlink(tmpfile);
			free(data);
		}
	}
	/* ── cmp: binary comparison with local file ── */
	else if (strcmp(cmd, "cmp") == 0) {
		if (path_idx + 1 >= argc) { usage(); ret = 1; }
		else {
			const char *apath = argv[path_idx];
			const char *lfile = argv[path_idx + 1];
			void *adata; size_t asize;
			if (arch_load(a, apath, &adata, &asize) < 0) { ret = -1; }
			FILE *lf = fopen(lfile, "rb");
			if (!lf) { perror(lfile); free(adata); ret = -1;  }
			fseek(lf, 0, SEEK_END); long lsize = ftell(lf); fseek(lf, 0, SEEK_SET);
			if ((size_t)lsize != asize) {
				printf("differ: size %ld != %zu\n", lsize, asize);
			} else {
				void *lbuf = malloc((size_t)lsize);
				fread(lbuf, 1, (size_t)lsize, lf);
				int diff_off = memcmp(adata, lbuf, asize);
				if (diff_off == 0) printf("identical\n");
				else {
					for (size_t i = 0; i < asize; i++)
						if (((unsigned char*)adata)[i] != ((unsigned char*)lbuf)[i])
							{ printf("differ at offset %zu\n", i); break; }
				}
				free(lbuf);
			}
			fclose(lf);
			free(adata);
		}
	}
	/* ── du: directory usage ── */
	else if (strcmp(cmd, "du") == 0) {
		const char *dir = path_idx < argc ? argv[path_idx] : "";
		struct msys *m = a->is_overlay ? msys_overlay_get(a->overlay, 0) : a->single;
		uint32_t cnt = msys_count(m);
		uint64_t total = 0; int nfiles = 0; size_t dlen = strlen(dir);
		for (uint32_t i = 0; i < cnt; i++) {
			const char *name; size_t nlen, dsize;
			if (msys_enumerate(m, i, &name, &nlen, &dsize) < 0) continue;
			if (nlen > 0 && name[0] == '@') continue;
			/* Check prefix */
			if (dlen > 0) {
				if (nlen <= dlen || memcmp(name, dir, dlen) != 0) continue;
				if (name[dlen] != '/' && name[dlen] != '\0') continue;
			}
			total += dsize; nfiles++;
		}
		printf("%s\t%lu bytes, %d files\n", dir[0] ? dir : "/", (unsigned long)total, nfiles);
	}
	/* ── head: show first N lines ── */
	else if (strcmp(cmd, "head") == 0) {
		if (path_idx >= argc) { usage(); ret = 1; }
		else {
			int nlines = 10;
			const char *path = argv[path_idx];
			if (path_idx + 2 < argc && strcmp(argv[path_idx + 1], "-n") == 0)
				{ nlines = atoi(argv[path_idx + 2]); path = argv[path_idx]; }
			void *data; size_t dsize;
			if (arch_load(a, path, &data, &dsize) < 0) { ret = -1; }
			else {
				const char *p = (const char *)data, *end = p + dsize;
				int lines = 0;
				while (p < end && lines < nlines) {
					const char *nl = memchr(p, '\n', (size_t)(end - p));
					size_t llen = nl ? (size_t)(nl - p + 1) : (size_t)(end - p);
					fwrite(p, 1, llen, stdout);
					p += llen; lines++;
				}
				free(data);
			}
		}
	}
	/* ── tail: show last N lines ── */
	else if (strcmp(cmd, "tail") == 0) {
		if (path_idx >= argc) { usage(); ret = 1; }
		else {
			int nlines = 10;
			const char *path = argv[path_idx];
			if (path_idx + 2 < argc && strcmp(argv[path_idx + 1], "-n") == 0)
				{ nlines = atoi(argv[path_idx + 2]); path = argv[path_idx]; }
			void *data; size_t dsize;
			if (arch_load(a, path, &data, &dsize) < 0) { ret = -1; }
			else {
				const char *p = (const char *)data, *end = p + dsize;
				/* Count total lines */
				int total_lines = 0;
				for (const char *cp = p; cp < end; cp++) if (*cp == '\n') total_lines++;
				if (*(end-1) != '\n') total_lines++;
				/* Scan to start line */
				int skip = total_lines > nlines ? total_lines - nlines : 0;
				const char *start = p;
				for (int i = 0; i < skip && start < end; start++) if (*start == '\n') i++;
				fwrite(start, 1, (size_t)(end - start), stdout);
				free(data);
			}
		}
	}
	/* ── cp: copy file from archive to filesystem ── */
	else if (strcmp(cmd, "cp") == 0) {
		if (path_idx + 1 >= argc) { usage(); ret = 1; }
		else {
			const char *src = argv[path_idx];
			const char *dst = argv[path_idx + 1];
			void *data; size_t dsize;
			if (arch_load(a, src, &data, &dsize) < 0) { ret = -1; }
			else {
				FILE *fp = fopen(dst, "wb");
				if (!fp) { perror(dst); free(data); ret = -1; }
				else { fwrite(data, 1, dsize, fp); fclose(fp); printf("copied %s (%zu bytes) -> %s\n", src, dsize, dst); free(data); }
			}
		}
	}
	/* ── wc: word/line/byte count ── */
	else if (strcmp(cmd, "wc") == 0) {
		if (path_idx >= argc) { usage(); ret = 1; }
		else {
			void *data; size_t dsize;
			if (arch_load(a, argv[path_idx], &data, &dsize) < 0) { ret = -1; }
			else {
				int lines = 0, words = 0;
				int in_word = 0;
				for (size_t i = 0; i < dsize; i++) {
					unsigned char c = ((unsigned char*)data)[i];
					if (c == '\n') lines++;
					if (c == ' ' || c == '\t' || c == '\n') { in_word = 0; }
					else if (!in_word) { in_word = 1; words++; }
				}
				printf("  %d  %d  %zu  %s\n", lines, words, dsize, argv[path_idx]);
				free(data);
			}
		}
	}
	/* ── sort: sorted output ── */
	else if (strcmp(cmd, "sort") == 0) {
		if (path_idx >= argc) { usage(); ret = 1; }
		else {
			void *data; size_t dsize;
			if (arch_load(a, argv[path_idx], &data, &dsize) < 0) { ret = -1; }
			else {
				/* Split into lines, sort, print */
				char *copy = malloc(dsize + 1);
				if (!copy) { free(data); ret = -1; }
				else {
					memcpy(copy, data, dsize); copy[dsize] = '\0';
					/* Count lines */
					int nlines = 0;
					for (size_t i = 0; i < dsize; i++) if (copy[i] == '\n') nlines++;
					if (dsize > 0 && copy[dsize-1] != '\n') nlines++;

					/* Build line array */
					char **lines = malloc((size_t)nlines * sizeof(char *));
					if (!lines) { free(copy); free(data); ret = -1; }
					else {
						int l = 0; char *p = copy;
						while (p < copy + dsize) {
							lines[l++] = p;
							char *nl = memchr(p, '\n', (size_t)(copy + dsize - p));
							if (nl) { *nl = '\0'; p = nl + 1; } else { p = copy + dsize; }
						}
						/* Simple bubble sort (line count is typically small) */
						for (int i = 0; i < nlines - 1; i++)
							for (int j = 0; j < nlines - i - 1; j++)
								if (strcmp(lines[j], lines[j+1]) > 0) {
									char *tmp = lines[j]; lines[j] = lines[j+1]; lines[j+1] = tmp;
								}
						for (int i = 0; i < nlines; i++) printf("%s\n", lines[i]);
						free(lines);
					}
					free(copy);
				}
				free(data);
			}
		}
	}
	else { fprintf(stderr, "Unknown: %s\n", cmd); usage(); }

	if (ret < 0) perror(cmd);
	close_archive(a);
	return ret < 0 ? 1 : 0;
}
static int cmd_info(struct archive *a) {
	/* Determine which layers to analyze */
	int nlayers = 1;
	struct msys *layers[16];
	if (a->is_overlay) {
		nlayers = msys_overlay_count(a->overlay);
		for (int i = 0; i < nlayers && i < 16; i++)
			layers[i] = msys_overlay_get(a->overlay, i);
	} else {
		layers[0] = a->single;
	}

	for (int li = 0; li < nlayers; li++) {
		struct msys *m = layers[li];
		uint32_t cnt = msys_count(m);
		int v2 = (msys_format_version(m) == MSYS_FORMAT_V2);
		uint32_t flags = m->hdr->flags;

		if (nlayers > 1) printf("\nLayer %d:\n", li);

		/* Header info */
		printf("  Format:      v%d\n", msys_format_version(m));
		printf("  Entries:     %u\n", cnt);
		printf("  Flags:       0x%04x", flags);
		if (flags & MSYS_F_ZLIB) printf(" zlib");
		if (flags & MSYS_F_ZSTD) printf(" zstd");
		if (flags & MSYS_F_DEDUP) printf(" dedup");
		if (flags & MSYS_F_DIR_BLOCK) printf(" dir-block");
		if (flags & MSYS_F_STREAMING) printf(" streaming");
		if (flags & MSYS_F_SIGNED) printf(" signed");
		printf("\n");

		/* Count file types */
		int nreg = 0, ndir = 0, nsym = 0, nmeta = 0;
		uint64_t total_data = 0, total_stored = 0;
		uint64_t dedup_saved = 0, dedup_files = 0;
		uint64_t largest_size = 0;
		char largest_name[256] = "";
		/* Track unique data offsets for dedup calculation */
		uint64_t *unique_offs = NULL;
		size_t unique_cnt = 0, unique_cap = 0;

		for (uint32_t i = 0; i < cnt; i++) {
			const char *name; size_t nlen, dsize;
			if (msys_enumerate(m, i, &name, &nlen, &dsize) < 0) continue;
			char nb[4096];
			if (nlen >= sizeof(nb)) continue;
			memcpy(nb, name, nlen); nb[nlen] = '\0';

			if (nb[0] == '@') { nmeta++; continue; }
			if (v2) {
				struct msys_stat st;
				if (msys_stat(m, nb, &st) == 0) {
					if (st.file_type == MSYS_FILE_DIR) ndir++;
					else if (st.file_type == MSYS_FILE_SYMLINK) nsym++;
					else nreg++;
				} else nreg++;
			} else nreg++;

			total_data += dsize;

			/* Track stored size from index entry */
			int entry_v2 = v2;
			unsigned char *ep = m->entries[i];
			uint32_t stored_dsize = entry_v2 ? 
				(uint32_t)ep[10] | ((uint32_t)ep[11]<<8) | ((uint32_t)ep[12]<<16) | ((uint32_t)ep[13]<<24) :
				(uint32_t)ep[10] | ((uint32_t)ep[11]<<8) | ((uint32_t)ep[12]<<16) | ((uint32_t)ep[13]<<24);
			total_stored += stored_dsize;

			/* Track largest */
			if (dsize > largest_size) {
				largest_size = dsize;
				snprintf(largest_name, sizeof(largest_name), "%.*s", (int)nlen, name);
			}

			/* Dedup tracking: check if this data offset is unique */
			uint64_t data_off = entry_v2 ?
				(uint64_t)ep[4] | ((uint64_t)ep[5]<<8) | ((uint64_t)ep[6]<<16) | ((uint64_t)ep[7]<<24) | ((uint64_t)ep[8]<<32) | ((uint64_t)ep[9]<<40) :
				(uint64_t)ep[4] | ((uint64_t)ep[5]<<8) | ((uint64_t)ep[6]<<16) | ((uint64_t)ep[7]<<24) | ((uint64_t)ep[8]<<32) | ((uint64_t)ep[9]<<40);
			int found = 0;
			for (size_t j = 0; j < unique_cnt; j++) {
				if (unique_offs[j] == data_off) { found = 1; break; }
			}
			if (!found) {
				if (unique_cnt >= unique_cap) {
					unique_cap = unique_cap ? unique_cap * 2 : 64;
					uint64_t *nu = realloc(unique_offs, unique_cap * sizeof(uint64_t));
					if (!nu) { free(unique_offs); return -1; }
					unique_offs = nu;
				}
				unique_offs[unique_cnt++] = data_off;
			} else {
				dedup_files++;
				dedup_saved += dsize;
			}
		}
		free(unique_offs);

		/* File type summary */
		printf("  Files:       %d regular, %d dir, %d symlink, %d meta\n",
		       nreg, ndir, nsym, nmeta);
		printf("  Data:        %lu bytes stored", (unsigned long)total_stored);
		if (flags & (MSYS_F_ZLIB | MSYS_F_ZSTD))
			printf(" (%lu uncompressed, %.1f%% ratio)",
			       (unsigned long)total_data,
			       total_data > 0 ? 100.0 * total_stored / total_data : 0);
		printf("\n");

		/* Dedup info */
		if (dedup_files > 0)
			printf("  Dedup:       %lu files shared, %lu bytes saved\n",
			       (unsigned long)dedup_files, (unsigned long)dedup_saved);

		/* Extension blocks */
		if (v2) {
			uint32_t ext_off = m->hdr_v2->extension_offset;
			if (ext_off > 0) {
				const unsigned char *p = (const unsigned char *)m->base + ext_off;
				uint64_t avail = m->size - ext_off;
				int n_ext = 0;
				while (avail >= 8) {
					uint32_t bt = (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
					uint32_t bl = (uint32_t)p[4] | ((uint32_t)p[5]<<8) | ((uint32_t)p[6]<<16) | ((uint32_t)p[7]<<24);
					char fourcc[5] = {(char)(bt&0xff),(char)((bt>>8)&0xff),(char)((bt>>16)&0xff),(char)((bt>>24)&0xff),0};
					printf("  Extension:   0x%08x (\"%s\") %u bytes\n", bt, fourcc, bl);
					p += 8 + bl; avail -= 8 + bl;
					n_ext++;
				}
				if (n_ext == 0) printf("  Extension:   0 bytes\n");
			}
		}

		/* Largest file */
		if (largest_size > 0)
			printf("  Largest:     %s (%lu bytes)\n", largest_name, (unsigned long)largest_size);

		/* Dir block */
		if (v2 && m->hdr_v2->dir_count > 0)
			printf("  Dir block:   %u entries\n", m->hdr_v2->dir_count);

		/* Signature */
		{
			const void *sig_data; uint32_t sig_len;
			if (v2 && msys_get_extension(m, 0x6e676973, &sig_data, &sig_len) == 0)
				printf("  Signature:   present (%u bytes)\n", sig_len);
		}
	}
	return 0;
}

