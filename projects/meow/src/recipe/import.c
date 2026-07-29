/* meow import — autoconf configure.ac -> .meow recipe converter.
 *
 * Usage: meow import [--output DIR] path/to/configure.ac
 *
 * Reads an autoconf configure.ac, uses the libm4 engine to expand
 * m4 macros, parses the expanded output for AC_* patterns, and
 * emits a project.meow recipe (to stdout or to a file).
 */
#define _POSIX_C_SOURCE 200809L
#include "meow.h"
#include "libm4/m4_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>

#define IMPORT_MAX (1024 * 256)
#define EXPANDED_MAX (1024 * 512)

/* ---------- collected info ---------- */

struct ac_info {
	char name[256];
	char version[256];
	char headers[2048];
	char functions[2048];
	char config_files[2048];
	char subst_vars[2048];
	char defines[4096];
	int  has_prog_cc;
	int  has_config_h;
	int  has_output;
};

/* ---------- line helpers ---------- */

/* Skip whitespace */
static const char *
skip_ws(const char *p)
{
	while (*p == ' ' || *p == '\t')
		p++;
	return p;
}

/* Trim trailing whitespace / newline in place */
static void
chomp(char *s)
{
	size_t n = strlen(s);
	while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' ||
	       s[n - 1] == ' ' || s[n - 1] == '\t'))
		s[--n] = '\0';
}

/* ---------- AC_* pattern matchers ---------- */

/* Extract comma-separated contents inside macro(args).
 * Handles bracket-quoted ([foo]) and unquoted args.
 * Returns array of strings; sets *nargs.  Caller free()s result and
 * each element. */
static char **
parse_ac_args(const char *line, const char *macro, int *nargs)
{
	*nargs = 0;
	const char *p = strstr(line, macro);
	if (!p)
		return NULL;
	p += strlen(macro);
	p = skip_ws(p);
	if (*p != '(')
		return NULL;
	p++; /* skip '(' */

	int cap = 8;
	int n = 0;
	char **args = calloc((size_t)cap, sizeof(char *));

	while (*p && *p != ')') {
		p = skip_ws(p);
		if (*p == ',' || *p == ')') {
			if (*p == ',') p++;
			continue;
		}
		if (*p == '\0' || *p == '\n')
			break;

		char buf[1024];
		size_t pos = 0;
		int bracketed = (*p == '[');

		if (bracketed)
			p++;

		while (*p && pos < sizeof(buf) - 1) {
			if (bracketed && *p == ']') { p++; break; }
			if (!bracketed && (*p == ',' || *p == ')' ||
			    *p == '\0' || *p == '\n'))
				break;
			buf[pos++] = *p++;
		}
		buf[pos] = '\0';

		if (n >= cap) {
			cap *= 2;
			args = realloc(args, (size_t)cap * sizeof(char *));
		}
		args[n++] = strdup(buf);
	}

	/* Free trailing NULLs allocated by calloc */
	*nargs = n;
	return args;
}

/* Parse AC_CHECK_HEADERS([list]) or AC_CHECK_HEADERS(list).
 * Extracts the space-separated headers from the first arg and appends
 * them to info->headers (comma+space separated). */
static void
gather_headers(struct ac_info *info, const char *line)
{
	int n;
	char **args = parse_ac_args(line, "AC_CHECK_HEADERS", &n);
	if (!args || n < 1)
		goto out;
	/* First arg is space-separated list */
	const char *p = args[0];
	while (*p) {
		while (*p == ' ' || *p == '\t')
			p++;
		if (!*p)
			break;
		const char *start = p;
		while (*p && *p != ' ' && *p != '\t')
			p++;
		size_t len = (size_t)(p - start);
		if (len > 0) {
			if (info->headers[0])
				strncat(info->headers, ", ", sizeof(info->headers) -
				        strlen(info->headers) - 1);
			strncat(info->headers, start, len);
		}
	}
out:
	if (args) {
		for (int i = 0; i < n; i++)
			free(args[i]);
		free(args);
	}
}

/* Parse AC_CHECK_FUNCS([list]) */
static void
gather_funcs(struct ac_info *info, const char *line)
{
	int n;
	char **args = parse_ac_args(line, "AC_CHECK_FUNCS", &n);
	if (!args || n < 1)
		goto out;
	const char *p = args[0];
	while (*p) {
		while (*p == ' ' || *p == '\t')
			p++;
		if (!*p)
			break;
		const char *start = p;
		while (*p && *p != ' ' && *p != '\t')
			p++;
		size_t len = (size_t)(p - start);
		if (len > 0) {
			if (info->functions[0])
				strncat(info->functions, ", ", sizeof(info->functions) -
				        strlen(info->functions) - 1);
			strncat(info->functions, start, len);
		}
	}
out:
	if (args) {
		for (int i = 0; i < n; i++)
			free(args[i]);
		free(args);
	}
}

/* Parse AC_INIT([name], [ver], ...) */
static void
gather_init(struct ac_info *info, const char *line)
{
	int n;
	char **args = parse_ac_args(line, "AC_INIT", &n);
	if (!args)
		return;
	if (n >= 1)
		strncpy(info->name, args[0], sizeof(info->name) - 1);
	if (n >= 2)
		strncpy(info->version, args[1], sizeof(info->version) - 1);
	for (int i = 0; i < n; i++)
		free(args[i]);
	free(args);
}

/* Parse AC_CONFIG_FILES([list]) -- comma-separated file list in one arg */
static void
gather_config_files(struct ac_info *info, const char *line)
{
	int n;
	char **args = parse_ac_args(line, "AC_CONFIG_FILES", &n);
	if (!args || n < 1)
		goto out;
	strncpy(info->config_files, args[0], sizeof(info->config_files) - 1);
out:
	if (args) {
		for (int i = 0; i < n; i++)
			free(args[i]);
		free(args);
	}
}

/* Parse AC_CONFIG_HEADERS([h]) — just flags has_config_h */
/* (no explicit handler needed; the generic AC_CONFIG_HEADERS strstr
 *  in parse_ac_line sets has_config_h = 1) */

/* Parse AC_SUBST(VAR) */
static void
gather_subst(struct ac_info *info, const char *line)
{
	int n;
	char **args = parse_ac_args(line, "AC_SUBST", &n);
	if (!args || n < 1)
		goto out;
	if (info->subst_vars[0])
		strncat(info->subst_vars, ", ", sizeof(info->subst_vars) -
		        strlen(info->subst_vars) - 1);
	strncat(info->subst_vars, args[0], sizeof(info->subst_vars) -
	        strlen(info->subst_vars) - 1);
out:
	if (args) {
		for (int i = 0; i < n; i++)
			free(args[i]);
		free(args);
	}
}

/* Parse AC_DEFINE(VAR, val) */
static void
gather_define(struct ac_info *info, const char *line)
{
	int n;
	char **args = parse_ac_args(line, "AC_DEFINE", &n);
	if (!args || n < 1)
		goto out;
	if (info->defines[0])
		strncat(info->defines, "\n", sizeof(info->defines) -
		        strlen(info->defines) - 1);
	strncat(info->defines, args[0], sizeof(info->defines) -
	        strlen(info->defines) - 1);
	if (n >= 2) {
		strncat(info->defines, " = ", sizeof(info->defines) -
		        strlen(info->defines) - 1);
		strncat(info->defines, args[1], sizeof(info->defines) -
		        strlen(info->defines) - 1);
	}
out:
	if (args) {
		for (int i = 0; i < n; i++)
			free(args[i]);
		free(args);
	}
}

/* Dispatch a single line through all AC_* recognizers */
static void
parse_ac_line(struct ac_info *info, const char *line)
{
	/* Strip dnl comments */
	const char *dnl = strstr(line, "dnl");
	if (dnl) {
		char buf[4096];
		size_t len = (size_t)(dnl - line);
		if (len >= sizeof(buf))
			len = sizeof(buf) - 1;
		memcpy(buf, line, len);
		buf[len] = '\0';
		line = buf;
	}

	/* Strip # comments (but beware of string contents) */
	const char *hash = strchr(line, '#');
	if (hash) {
		/* Only treat as comment if # is at start of line or after whitespace */
		const char *check = line;
		while (check < hash && (*check == ' ' || *check == '\t'))
			check++;
		if (check == hash) {
			char buf[4096];
			size_t len = (size_t)(hash - line);
			if (len >= sizeof(buf))
				len = sizeof(buf) - 1;
			memcpy(buf, line, len);
			buf[len] = '\0';
			line = buf;
		}
	}

	if (strstr(line, "AC_INIT"))
		gather_init(info, line);
	if (strstr(line, "AC_PROG_CC"))
		info->has_prog_cc = 1;
	if (strstr(line, "AC_CHECK_HEADERS"))
		gather_headers(info, line);
	if (strstr(line, "AC_CHECK_FUNCS"))
		gather_funcs(info, line);
	if (strstr(line, "AC_CONFIG_FILES"))
		gather_config_files(info, line);
	if (strstr(line, "AC_CONFIG_HEADERS") || strstr(line, "AC_CONFIG_HEADER"))
		info->has_config_h = 1;
	if (strstr(line, "AC_SUBST"))
		gather_subst(info, line);
	if (strstr(line, "AC_DEFINE"))
		gather_define(info, line);
	if (strstr(line, "AC_OUTPUT"))
		info->has_output = 1;
}

/* ---------- file I/O ---------- */

/* Read entire file into a malloc'd buffer.  Caller free()s. */
static char *
read_file(const char *path, size_t *out_len)
{
	FILE *f = fopen(path, "r");
	if (!f) {
		meow_msg(MSG_ERROR, "cannot open %s: %s", path, strerror(errno));
		return NULL;
	}

	/* Get size */
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return NULL;
	}
	long ft = ftell(f);
	if (ft < 0) {
		fclose(f);
		return NULL;
	}
	size_t fsize = (size_t)ft;
	rewind(f);

	if (fsize > IMPORT_MAX) {
		meow_msg(MSG_ERROR, "%s too large (%zu bytes, max %d)",
		         path, fsize, IMPORT_MAX);
		fclose(f);
		return NULL;
	}

	char *buf = malloc(fsize + 16);
	if (!buf) {
		fclose(f);
		return NULL;
	}
	size_t nread = fread(buf, 1, fsize, f);
	buf[nread] = '\0';
	fclose(f);

	if (out_len)
		*out_len = nread;
	return buf;
}

/* Try to find and inline m4_include([file]) references.
 * If found, replaces the include line with the included content.
 * Returns offset past the last processed byte in *buf (for incremental
 * use). This is a simple single-pass; we're not doing a full preprocessor. */
static char *
resolve_includes(const char *ac_path, const char *src, size_t srclen)
{
	(void)srclen;
	/* Allocate output buffer */
	char *out = malloc(IMPORT_MAX + 16384);
	if (!out)
		return NULL;
	out[0] = '\0';

	const char *p = src;
	char dir[1024];
	strncpy(dir, ac_path, sizeof(dir) - 1);
	dir[sizeof(dir) - 1] = '\0';
	char *d = dirname(dir);

	while (*p) {
		/* Check for m4_include or sinclude */
		const char *inc = NULL;
		if ((inc = strstr(p, "m4_include(")) || (inc = strstr(p, "m4_sinclude("))) {
			/* Copy everything before the include */
			size_t pre = (size_t)(inc - p);
			strncat(out, p, pre);

			/* Find the filename */
			const char *ap = inc + strlen(inc == p ? "m4_include" : "m4_sinclude");
			ap = skip_ws(ap);
			if (*ap == '(') ap++;
			ap = skip_ws(ap);
			const char *fstart = ap;
			if (*fstart == '[') fstart++;
			const char *fend = fstart;
			while (*fend && *fend != ')' && *fend != ']')
				fend++;
			size_t fnlen = (size_t)(fend - fstart);
			if (fnlen > 0 && fnlen < 1024) {
				char incpath[1024];
				snprintf(incpath, sizeof(incpath), "%s/", d);
				strncat(incpath, fstart, fnlen);
				char fname[1024];
				strncpy(fname, fstart, fnlen);
				fname[fnlen] = '\0';

				size_t ilen;
				char *icontent = read_file(incpath, &ilen);
				if (icontent) {
					strncat(out, icontent, IMPORT_MAX + 16384 - strlen(out) - 1);
					free(icontent);
				} else {
					/* Could not read included file; skip */
					meow_msg(MSG_WARN, "could not read included file: %s", incpath);
				}

				/* Advance p past the include directive */
				p = fend;
				if (*p == ']') p++;
				if (*p == ')') p++;
			} else {
				/* Not a valid include; advance past ( */
				p = inc + 1;
			}
		} else {
			/* No more includes; copy the rest */
			strcat(out, p);
			break;
		}
	}

	return out;
}

/* ---------- output generation ---------- */

static void
emit_recipe(FILE *out, const struct ac_info *info)
{
	fprintf(out, "# Generated by meow import from configure.ac\n");

	if (info->name[0])
		fprintf(out, "name: %s\n", info->name);
	if (info->version[0])
		fprintf(out, "version: %s\n", info->version);

	fputc('\n', out);

	/* --- [probe] --- */
	int has_probe = info->has_prog_cc || info->headers[0] ||
	                info->functions[0];
	if (has_probe || info->has_config_h) {
		fprintf(out, "[probe]\n");

		if (info->has_prog_cc)
			fprintf(out, "  cc: %%CC%%\n");

		if (info->headers[0])
			fprintf(out, "  headers: [%s]\n", info->headers);

		if (info->functions[0])
			fprintf(out, "  functions: [%s]\n", info->functions);

		fputc('\n', out);
	}

	/* --- [variables] --- */
	if (info->subst_vars[0]) {
		fprintf(out, "[variables]\n");
		fprintf(out, "  %s\n", info->subst_vars);
		fputc('\n', out);
	}

	/* --- [defines] --- */
	if (info->defines[0]) {
		fprintf(out, "# AC_DEFINE references (hand-edit as needed)\n");
		fprintf(out, "%s\n", info->defines);
		fputc('\n', out);
	}

	/* --- [build] --- */
	fprintf(out, "[build]\n");
	fprintf(out, "  deps: all\n");

	/* Generate a build command based on what we know */
	fprintf(out, "  run: %%%%CC%%%% %%%%CFLAGS%%%%");
	if (info->name[0]) {
		char obj[256];
		snprintf(obj, sizeof(obj), " -o %s", info->name);
		fprintf(out, "%s", obj);
	}
	/* Add source file placeholders */
	if (info->config_files[0]) {
		/* Replace .in extension for each config file */
		fprintf(out, " ");
		const char *cf = info->config_files;
		while (*cf) {
			while (*cf == ' ' || *cf == '\t' || *cf == ',')
				cf++;
			if (!*cf)
				break;
			const char *start = cf;
			while (*cf && *cf != ' ' && *cf != '\t' && *cf != ',')
				cf++;
			size_t len = (size_t)(cf - start);
			if (len > 0 && len < 512) {
				char fn[512];
				memcpy(fn, start, len);
				fn[len] = '\0';
				/* strip .in suffix if present */
				size_t flen = strlen(fn);
				if (flen > 3 && strcmp(fn + flen - 3, ".in") == 0)
					fn[flen - 3] = '\0';
				fprintf(out, "%s.c ", fn);
			}
		}
	} else {
		fprintf(out, " example.c");
	}
	fputc('\n', out);

	fprintf(out, "  # Generated from AC_CONFIG_FILES: %s\n",
	        info->config_files[0] ? info->config_files : "none needed");

	if (info->has_config_h) {
		fprintf(out, "\n[target: config.h]\n");
		fprintf(out, "  deps: config.h.in\n");
		fprintf(out, "  run: meow template config.h.in config.h\n");
	}
}

/* ---------- command entry point ---------- */

int
cmd_import(int argc, char **argv)
{
	const char *output_dir = NULL;
	const char *input_file = NULL;
	int i = 0;

	/* Parse flags */
	while (i < argc) {
		if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
			output_dir = argv[i + 1];
			i += 2;
		} else if (strncmp(argv[i], "--output=", 9) == 0) {
			output_dir = argv[i] + 9;
			i++;
		} else if (strcmp(argv[i], "--help") == 0) {
			printf("usage: meow import [--output DIR] path/to/configure.ac\n");
			printf("\n");
			printf("Read an autoconf configure.ac and generate a .meow recipe.\n");
			return 0;
		} else if (!input_file) {
			input_file = argv[i];
			i++;
		} else {
			meow_msg(MSG_ERROR, "unexpected argument: %s", argv[i]);
			return 1;
		}
	}

	if (!input_file) {
		meow_msg(MSG_ERROR, "usage: meow import [--output DIR] path/to/configure.ac");
		return 1;
	}

	/* --- Step 1: read configure.ac --- */
	size_t ac_len;
	char *ac_content = read_file(input_file, &ac_len);
	if (!ac_content)
		return 1;

	/* --- Step 1b: resolve m4_include --- */
	char *full_content = resolve_includes(input_file, ac_content, ac_len);
	free(ac_content);
	if (!full_content) {
		meow_msg(MSG_ERROR, "out of memory");
		return 1;
	}

	/* --- Step 2: expand via m4 engine --- */
	char expanded[EXPANDED_MAX];
	expanded[0] = '\0';

	m4_init();
	/* Pre-define some common autoconf-visible macros to prevent
	 * the m4 engine from erroring on undefined references. */
	m4_define("__file__", input_file);
	m4_define("__line__", "0");

	int m4_ok = m4_process(full_content, expanded, sizeof(expanded));
	m4_reset();

	if (m4_ok != 0) {
		meow_msg(MSG_WARN, "m4 expansion returned error; falling back to raw parse");
		/* Fall back to unexpanded content */
		strncpy(expanded, full_content, sizeof(expanded) - 1);
	}

	/* --- Step 3: parse expanded output for AC_* patterns --- */
	struct ac_info info;
	memset(&info, 0, sizeof(info));

	/* Parse line by line */
	char line[4096];
	const char *p = expanded;
	while (*p) {
		size_t linelen = 0;
		while (p[linelen] && p[linelen] != '\n' && linelen < sizeof(line) - 1)
			linelen++;
		memcpy(line, p, linelen);
		line[linelen] = '\0';
		chomp(line);

		if (line[0] != '\0')
			parse_ac_line(&info, line);

		p += linelen;
		if (*p == '\n')
			p++;
	}

	free(full_content);

	/* --- Step 4: emit recipe --- */
	FILE *out = stdout;
	char outpath[1024] = "";

	if (output_dir) {
		snprintf(outpath, sizeof(outpath), "%s/project.meow", output_dir);
		out = fopen(outpath, "w");
		if (!out) {
			meow_msg(MSG_ERROR, "cannot open output %s: %s",
			         outpath, strerror(errno));
			return 1;
		}
	}

	emit_recipe(out, &info);

	if (output_dir) {
		fclose(out);
		meow_msg(MSG_SUCCESS, "wrote %s", outpath);
	}

	return 0;
}
