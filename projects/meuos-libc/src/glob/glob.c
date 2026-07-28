/* glob/glob.c — POSIX glob implementation */

#include <glob.h>
#include <fnmatch.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

static int
glob_append(struct glob_t *g, const char *path)
{
	if (g->gl_pathc >= g->capacity) {
		size_t cap = g->capacity ? g->capacity * 2 : 32;
		char **nv = realloc(g->gl_pathv, cap * sizeof(char *));
		if (!nv) return GLOB_NOSPACE;
		g->gl_pathv = nv;
		g->capacity = cap;
	}
	g->gl_pathv[g->gl_pathc] = strdup(path);
	if (!g->gl_pathv[g->gl_pathc])
		return GLOB_NOSPACE;
	g->gl_pathc++;
	return 0;
}

/* Match pattern against directory entries. Recursively handles "star/slash/star" etc. */
static int
glob_do(const char *pattern, struct glob_t *g, int flags)
{
	const char *slash = strchr(pattern, '/');
	if (!slash) {
		/* No slash: simple filename pattern */
		DIR *d = opendir(".");
		if (!d) return 0;
		struct dirent *e;
		while ((e = readdir(d))) {
			if (e->d_name[0] == '.' && (!pattern || pattern[0] != '.'))
				continue;
			if (fnmatch(pattern, e->d_name, flags & ~FNM_PATHNAME) == 0) {
				int r = glob_append(g, e->d_name);
				if (r) { closedir(d); return r; }
			}
		}
		closedir(d);
		if (g->gl_pathc == 0 && !(flags & GLOB_NOCHECK))
			return GLOB_NOMATCH;
		return 0;
	}

	/* Has slash: match directory part first, then file part */
	size_t dirlen = (size_t)(slash - pattern);
	char dirpat[1024];
	if (dirlen >= sizeof(dirpat))
		return GLOB_NOMATCH;
	memcpy(dirpat, pattern, dirlen);
	dirpat[dirlen] = '\0';
	const char *filepat = slash + 1;

	DIR *d;
	int (*errfunc)(const char *, int) = NULL;

	/* If dirpat contains wildcards, recurse */
	if (strpbrk(dirpat, "*?[")) {
		struct glob_t sub = {0};
		int r = glob_do(dirpat, &sub, flags);
		if (r) return r;
		for (size_t i = 0; i < sub.gl_pathc; i++) {
			d = opendir(sub.gl_pathv[i]);
			if (!d) continue;
			struct dirent *e;
			while ((e = readdir(d))) {
				if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
					continue;
				if (fnmatch(filepat, e->d_name, flags & ~FNM_PATHNAME) == 0) {
					char full[1024];
					snprintf(full, sizeof(full), "%s/%s", sub.gl_pathv[i], e->d_name);
					int r2 = glob_append(g, full);
					if (r2) { closedir(d); globfree(&sub); return r2; }
				}
			}
			closedir(d);
		}
		globfree(&sub);
	} else {
		/* Exact directory */
		d = opendir(dirpat);
		if (!d) return 0;
		struct dirent *e;
		while ((e = readdir(d))) {
			if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
				continue;
			if (fnmatch(filepat, e->d_name, flags & ~FNM_PATHNAME) == 0) {
				char full[1024];
				snprintf(full, sizeof(full), "%s/%s", dirpat, e->d_name);
				int r2 = glob_append(g, full);
				if (r2) { closedir(d); return r2; }
			}
		}
		closedir(d);
	}
	return 0;
}

int
glob(const char *restrict pattern, int flags,
     int (*errfunc)(const char *, int),
     struct glob_t *restrict g)
{
	if (!g) return GLOB_ABORTED;
	(void)errfunc;

	if (!(flags & GLOB_APPEND)) {
		g->gl_pathc = 0;
		g->gl_pathv = NULL;
		g->capacity = 0;
	}
	g->gl_offs = flags & GLOB_DOOFFS ? g->gl_offs : 0;

	return glob_do(pattern, g, flags);
}

void
globfree(struct glob_t *g)
{
	if (!g) return;
	for (size_t i = 0; i < g->gl_pathc; i++)
		free(g->gl_pathv[i]);
	free(g->gl_pathv);
	g->gl_pathv = NULL;
	g->gl_pathc = 0;
	g->capacity = 0;
}
