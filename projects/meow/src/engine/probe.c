/* probe.c — meow inline feature detection (autoconf replacement).
 *
 * Parses the YAML "probe:" section and runs compile/link tests to
 * determine available headers, functions, types, and arbitrary code
 * features, then generates config.h for the recipe. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "meow.h"

/* Temporary file helpers */
static char tmpdir[] = "/tmp/meow-probe.XXXXXX";

/* Maximum argument count for compiler command. */
#define PROBE_ARGS_MAX 64

/* Reject strings containing shell metacharacters. */
static int
is_safe_arg(const char *s)
{
	if (!s || !*s) return 1;
	for (; *s; s++) {
		if (*s <= ' ' || *s == '|' || *s == ';' || *s == '&' ||
		    *s == '$' || *s == '`' || *s == '\'' || *s == '"' ||
		    *s == '(' || *s == ')' || *s == '<' || *s == '>' ||
		    *s == '\\' || *s == '\n' || *s == '\t')
			return 0;
	}
	return 1;
}

/* Split a string by spaces into an argv array (max PROBE_ARGS_MAX entries).
 * The string is modified in-place. Returns argc on success, -1 on overflow. */
static int
split_args(char *buf, const char *argv[], int max_args)
{
	int argc = 0;
	char *p = buf;
	while (*p) {
		while (*p == ' ') *p++ = '\0';
		if (!*p) break;
		if (argc >= max_args - 1) return -1;
		argv[argc++] = p;
		while (*p && *p != ' ') p++;
	}
	argv[argc] = NULL;
	return argc;
}

/* Build argv and fork/exec a compiler.  Returns 0 on success, -1 on failure.
 * stderr is redirected to /dev/null (probes are silent). */
static int
exec_compiler(const char *cc, const char *cflags,
              const char *suffix, const char *src, const char *obj_or_exe)
{
	char cc_copy[256], cflags_copy[2048];
	const char *cc_argv[PROBE_ARGS_MAX], *cflags_argv[PROBE_ARGS_MAX];
	const char *argv[PROBE_ARGS_MAX];
	int argc = 0;
	pid_t pid;
	int status;

	if (!is_safe_arg(cc ? cc : "") || !is_safe_arg(cflags ? cflags : ""))
		return -1;

	snprintf(cc_copy, sizeof cc_copy, "%s", cc && cc[0] ? cc : "mcc");
	snprintf(cflags_copy, sizeof cflags_copy, "%s", cflags ? cflags : "");

	int cc_argc = split_args(cc_copy, cc_argv, PROBE_ARGS_MAX);
	if (cc_argc <= 0) return -1;
	for (int i = 0; i < cc_argc; i++) argv[argc++] = cc_argv[i];

	int cflags_argc = split_args(cflags_copy, cflags_argv, PROBE_ARGS_MAX);
	for (int i = 0; i < cflags_argc; i++) argv[argc++] = cflags_argv[i];

	argv[argc++] = suffix; /* "-c" for compile, (empty) for link */
	if (obj_or_exe[0]) {
		argv[argc++] = "-o";
		argv[argc++] = obj_or_exe;
	}
	argv[argc++] = src;
	argv[argc] = NULL;

	pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		int fd = open("/dev/null", O_WRONLY);
		if (fd >= 0) { dup2(fd, 2); close(fd); }
		execvp(argv[0], (char *const *)argv);
		_exit(127);
	}
	waitpid(pid, &status, 0);
	return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static int
probe_init(void)
{
	if (!mkdtemp(tmpdir))
		return -1;
	return 0;
}

static void
probe_fini(void)
{
	if (tmpdir[0] == '/') {
		char cmd[256];
		snprintf(cmd, sizeof cmd, "rm -rf %s", tmpdir);
		run(cmd);
		memcpy(tmpdir, "/tmp/meow-probe.XXXXXX", sizeof "/tmp/meow-probe.XXXXXX");
	}
}

/* Compile a minimal C snippet. Returns 0 on success, -1 on failure.
 * The snippet is written to probe-test.c, compiled to probe-test.o,
 * then both are deleted. */
static int
probe_compile(const char *code, const char *cflags, const char *cc)
{
	char src[256], obj[256];
	FILE *f;
	int rc;

	snprintf(src, sizeof src, "%s/probe-test.c", tmpdir);
	snprintf(obj, sizeof obj, "%s/probe-test.o", tmpdir);

	f = fopen(src, "w");
	if (!f) return -1;
	fprintf(f, "%s\n", code);
	fclose(f);

	rc = exec_compiler(cc, cflags, "-c", src, obj);
	unlink(obj);
	unlink(src);
	return rc;
}

/* Compile and link a minimal C program to test library availability.
 * Unlike probe_compile, this runs the linker (no -c flag). */
static int
probe_link(const char *code, const char *cflags, const char *cc)
{
	char src[256], exe[256];
	FILE *f;
	int rc;

	snprintf(src, sizeof src, "%s/probe-link.c", tmpdir);
	snprintf(exe, sizeof exe, "%s/probe-link.exe", tmpdir);

	f = fopen(src, "w");
	if (!f) return -1;
	fprintf(f, "%s\n", code);
	fclose(f);

	rc = exec_compiler(cc, cflags, "", src, exe);
	unlink(exe);
	unlink(src);
	return rc;
}

/* Name normalisation: convert any name to HAVE_<UPPERCASE_SAFE>.
 * Removes non-alphanumeric characters; '/' and '.' become '_'. */
static void
normalise_name(const char *src, char *dst, size_t dstsize)
{
	size_t pos = 0;
	/* Prepend "HAVE_" */
	const char prefix[] = "HAVE_";
	for (size_t i = 0; prefix[i] && pos + 1 < dstsize; ++i)
		dst[pos++] = prefix[i];
	/* Copy name, uppercasing and converting separators */
	for (; *src && pos + 1 < dstsize; ++src) {
		if (*src == '/' || *src == '.' || *src == '-')
			dst[pos++] = '_';
		else if (*src >= 'a' && *src <= 'z')
			dst[pos++] = *src - 'a' + 'A';
		else if ((*src >= 'A' && *src <= 'Z') ||
		         (*src >= '0' && *src <= '9') || *src == '_')
			dst[pos++] = *src;
	}
	dst[pos] = 0;
}

/* Emit a #define line into config.h and inject into recipe_environment. */
static void
emit_define(FILE *f, const char *defname)
{
	fprintf(f, "#define %s 1\n", defname);
	char envline[128];
	snprintf(envline, sizeof envline, "export %s=1; ", defname);
	size_t len = strlen(recipe_environment);
	if (len + strlen(envline) < sizeof recipe_environment)
		memcpy(recipe_environment + len, envline, strlen(envline) + 1);
}

/* --- Probe data parsed from YAML --- */

#define PROBE_MAX 64

struct probe_item {
	char name[128];
	char code[1024];
};

static struct probe_item probe_headers[PROBE_MAX];
static size_t nprobe_headers;
static struct probe_item probe_funcs[PROBE_MAX];
static size_t nprobe_funcs;
static struct probe_item probe_codes[PROBE_MAX];
static size_t nprobe_codes;
static struct probe_item probe_decls[PROBE_MAX];
static size_t nprobe_decls;
static struct probe_item probe_libs[PROBE_MAX];
static size_t nprobe_libs;
static struct probe_item probe_typesizes[PROBE_MAX];
static size_t nprobe_typesizes;
static char probe_cc[128] = "";
static char probe_cflags[512] = "";
static char probe_config[64] = "config.h";

void
probe_reset(void)
{
	nprobe_headers = 0;
	nprobe_funcs = 0;
	nprobe_codes = 0;
	nprobe_decls = 0;
	nprobe_libs = 0;
	nprobe_typesizes = 0;
	probe_cc[0] = 0;
	probe_cflags[0] = 0;
	snprintf(probe_config, sizeof probe_config, "%s", "config.h");
}

int
probe_add_header(const char *name)
{
	if (nprobe_headers >= PROBE_MAX) return -1;
	snprintf(probe_headers[nprobe_headers].name, sizeof probe_headers[0].name, "%s", name);
	++nprobe_headers;
	return 0;
}

int
probe_add_function(const char *name)
{
	if (nprobe_funcs >= PROBE_MAX) return -1;
	snprintf(probe_funcs[nprobe_funcs].name, sizeof probe_funcs[0].name, "%s", name);
	++nprobe_funcs;
	return 0;
}

int
probe_add_code(const char *name, const char *code)
{
	if (nprobe_codes >= PROBE_MAX) return -1;
	snprintf(probe_codes[nprobe_codes].name, sizeof probe_codes[0].name, "%s", name);
	snprintf(probe_codes[nprobe_codes].code, sizeof probe_codes[0].code, "%s", code);
	++nprobe_codes;
	return 0;
}

int
probe_add_decl(const char *name)
{
	if (nprobe_decls >= PROBE_MAX) return -1;
	snprintf(probe_decls[nprobe_decls].name, sizeof probe_decls[0].name, "%s", name);
	++nprobe_decls;
	return 0;
}

int
probe_add_library(const char *name)
{
	if (nprobe_libs >= PROBE_MAX) return -1;
	snprintf(probe_libs[nprobe_libs].name, sizeof probe_libs[0].name, "%s", name);
	++nprobe_libs;
	return 0;
}

int
probe_add_typesize(const char *name, const char *type_name)
{
	if (nprobe_typesizes >= PROBE_MAX) return -1;
	snprintf(probe_typesizes[nprobe_typesizes].name,
	         sizeof probe_typesizes[0].name, "%s", name);
	snprintf(probe_typesizes[nprobe_typesizes].code,
	         sizeof probe_typesizes[0].code, "%s", type_name);
	++nprobe_typesizes;
	return 0;
}

int
probe_set_cc(const char *cc)
{
	snprintf(probe_cc, sizeof probe_cc, "%s", cc);
	return 0;
}

int
probe_set_cflags(const char *cflags)
{
	snprintf(probe_cflags, sizeof probe_cflags, "%s", cflags);
	return 0;
}

int
probe_set_config(const char *name)
{
	snprintf(probe_config, sizeof probe_config, "%s", name);
	return 0;
}

/* Compute a simple non-cryptographic hash of all probe registrations. */
static unsigned long
probe_fingerprint(void)
{
	unsigned long h = 5381;
	size_t i;
	for (i = 0; i < nprobe_headers; ++i)
		for (const char *p = probe_headers[i].name; *p; p++)
			h = ((h << 5) + h) + (unsigned char)*p;
	for (i = 0; i < nprobe_funcs; ++i)
		for (const char *p = probe_funcs[i].name; *p; p++)
			h = ((h << 5) + h) + (unsigned char)*p;
	for (i = 0; i < nprobe_codes; ++i) {
		for (const char *p = probe_codes[i].name; *p; p++)
			h = ((h << 5) + h) + (unsigned char)*p;
		for (const char *p = probe_codes[i].code; *p; p++)
			h = ((h << 5) + h) + (unsigned char)*p;
	}
	for (i = 0; i < nprobe_decls; ++i)
		for (const char *p = probe_decls[i].name; *p; p++)
			h = ((h << 5) + h) + (unsigned char)*p;
	for (i = 0; i < nprobe_libs; ++i)
		for (const char *p = probe_libs[i].name; *p; p++)
			h = ((h << 5) + h) + (unsigned char)*p;
	for (i = 0; i < nprobe_typesizes; ++i) {
		for (const char *p = probe_typesizes[i].name; *p; p++)
			h = ((h << 5) + h) + (unsigned char)*p;
		for (const char *p = probe_typesizes[i].code; *p; p++)
			h = ((h << 5) + h) + (unsigned char)*p;
	}
	return h;
}

/* Run all registered probes and generate config.h. Returns 0 on success. */
int
probe_run(const char *build_dir)
{
	char config_path[512];
	FILE *f;
	int rc = 0;
	size_t total = nprobe_headers + nprobe_funcs + nprobe_codes + nprobe_decls + nprobe_libs + nprobe_typesizes;

	if (total == 0)
		return 0; /* nothing to probe */

	snprintf(config_path, sizeof config_path, "%s/%s",
	         build_dir ? build_dir : ".",
	         probe_config);

	/* Probe cache: compute fingerprint of current probe declarations.
	 * If the cache file exists and its first line matches, skip all
	 * probes and reuse the cached config.h content. */
	unsigned long fp = probe_fingerprint();
	char fp_line[80];
	snprintf(fp_line, sizeof fp_line, "/* meow-fp: %lx */\n", fp);
	FILE *cf = fopen(config_path, "r");
	if (cf) {
		char first[80];
		if (fgets(first, sizeof first, cf) != NULL && strcmp(first, fp_line) == 0) {
			fclose(cf);
			fprintf(stderr, "meow: probe: cache hit for %s (skipping %zu probes)\n",
			        config_path, total);
			return 0;
		}
		fclose(cf);
	}

	if (probe_init() != 0)
		return -1;

	snprintf(config_path, sizeof config_path, "%s/%s",
	         build_dir ? build_dir : ".",
	         probe_config);

	f = fopen(config_path, "w");
	if (!f) { probe_fini(); return -1; }

	/* Emit fingerprint first so future runs can cache-hit without probing. */
	fprintf(f, "/* meow-fp: %lx */\n", fp);
	fprintf(f, "/* config.h */\n");
	fprintf(f, "#ifndef MEOW_CONFIG_H\n#define MEOW_CONFIG_H\n\n");

	/* 1. Probe headers: #include <name> + empty main */
	for (size_t i = 0; i < nprobe_headers; ++i) {
		char code[512];
		snprintf(code, sizeof code,
		         "#include <%s>\nint main(void){return 0;}\n",
		         probe_headers[i].name);
		if (probe_compile(code, probe_cflags, probe_cc) == 0) {
			char defname[128];
			normalise_name(probe_headers[i].name, defname, sizeof defname);
			emit_define(f, defname);
		}
	}

	/* 2. Probe functions: check symbol availability in libc.
	 *    Include common headers so the function declaration is visible. */
	for (size_t i = 0; i < nprobe_funcs; ++i) {
		char code[1024];
		snprintf(code, sizeof code,
		         "#include <sys/types.h>\n"
		         "#include <stdlib.h>\n"
		         "#include <stdio.h>\n"
		         "#include <string.h>\n"
		         "#include <unistd.h>\n"
		         "#include <time.h>\n"
		         "#include <fcntl.h>\n"
		         "#include <signal.h>\n"
		         "int main(void){void *p=(void*)%s;return p?0:0;}\n",
		         probe_funcs[i].name);
		if (probe_compile(code, probe_cflags, probe_cc) == 0) {
			char defname[128];
			normalise_name(probe_funcs[i].name, defname, sizeof defname);
			emit_define(f, defname);
		}
	}

	/* 3. Probe declarations: #include common headers + check if symbol
	 *    is declared (wrapping in a sizeof trick avoids link failures
	 *    for functions that exist at link time but lack declarations). */
	for (size_t i = 0; i < nprobe_decls; ++i) {
		char code[1024];
		snprintf(code, sizeof code,
		         "#include <sys/types.h>\n"
		         "#include <unistd.h>\n"
		         "#include <stdlib.h>\n"
		         "#include <stdio.h>\n"
		         "#include <string.h>\n"
		         "#include <time.h>\n"
		         "#include <fcntl.h>\n"
		         "#include <signal.h>\n"
		         "int main(void){"
		         "#ifndef %s\n"
		         "return -1;\n"
		         "#endif\n"
		         "return 0;}\n",
		         probe_decls[i].name);
		if (probe_compile(code, probe_cflags, probe_cc) == 0) {
			char defname[128];
			snprintf(defname, sizeof defname, "HAVE_DECL_%s", probe_decls[i].name);
			normalise_name(defname, defname, sizeof defname);
			emit_define(f, defname);
		}
	}

	/* 4. Probe custom code snippets: each is a complete .c file that
	 *    must compile successfully.  The user-specified name is used as
	 *    the #define token.  `\n` in the code value is converted to
	 *    actual newlines so multi-line tests work in YAML single-line. */
	for (size_t i = 0; i < nprobe_codes; ++i) {
		/* Convert literal \n to real newlines in-place */
		char *code = probe_codes[i].code;
		size_t wr = 0, rd = 0;
		while (code[rd]) {
			if (code[rd] == '\\' && code[rd+1] == 'n') {
				code[wr++] = '\n';
				rd += 2;
			} else {
				code[wr++] = code[rd++];
			}
		}
		code[wr] = 0;

		if (probe_compile(code, probe_cflags, probe_cc) == 0)
			emit_define(f, probe_codes[i].name);
	}

	/* 5. Probe libraries: link a minimal program with -l<name>. */
	for (size_t i = 0; i < nprobe_libs; ++i) {
		char code[512];
		snprintf(code, sizeof code,
		         "int main(void){return 0;}\n");
		char cflags[640];
		snprintf(cflags, sizeof cflags, "%s -l%s", probe_cflags, probe_libs[i].name);
		if (probe_link(code, cflags, probe_cc) == 0) {
			char defname[128];
			normalise_name(probe_libs[i].name, defname, sizeof defname);
			emit_define(f, defname);
		}
	}

	/* 6. Probe type sizes: compile + run a program that prints sizeof(type)
	 *    and use the output as the #define value. */
	for (size_t i = 0; i < nprobe_typesizes; ++i) {
		char code[512];
		snprintf(code, sizeof code,
		         "#include <sys/types.h>\n"
		         "#include <stdlib.h>\n"
		         "#include <stdio.h>\n"
		         "#include <string.h>\n"
		         "#include <time.h>\n"
		         "#include <unistd.h>\n"
		         "#include <fcntl.h>\n"
		         "#include <signal.h>\n"
		         "int main(void){printf(\"%%zu\\n\", sizeof(%s));return 0;}\n",
		         probe_typesizes[i].code);
		/* Compile to a temp executable */
		char src[512], exe[512];
		snprintf(src, sizeof src, "%s/probe-ts-%zu.c", tmpdir, i);
		snprintf(exe, sizeof exe, "%s/probe-ts-%zu.exe", tmpdir, i);
		FILE *sf = fopen(src, "w");
		if (sf) {
			fprintf(sf, "%s", code);
			fclose(sf);
			if (exec_compiler(
			         probe_cc[0] ? probe_cc : NULL,
			         probe_cflags, "", src, exe) == 0) {
				/* Run the compiled binary and capture output */
				char runcmd[1024];
				snprintf(runcmd, sizeof runcmd, "%s 2>/dev/null", exe);
				FILE *rf = popen(runcmd, "r");
				if (rf) {
					char val[64];
					if (fgets(val, sizeof val, rf) != NULL) {
						unsigned long sz = strtoul(val, NULL, 10);
						char defname[128];
						normalise_name(probe_typesizes[i].name, defname, sizeof defname);
						fprintf(f, "#define %s %lu\n", defname, sz);
					}
					pclose(rf);
				}
			}
			unlink(src);
			unlink(exe);
		}
	}

	fprintf(f, "\n#endif /* MEOW_CONFIG_H */\n");
	fclose(f);

	fprintf(stderr, "meow: probe: wrote %s (%zu checks, %zu headers, %zu funcs, %zu decls, %zu codes, %zu typesizes)\n",
	        config_path, total, nprobe_headers, nprobe_funcs, nprobe_decls, nprobe_codes, nprobe_typesizes);

	probe_fini();
	return rc;
}
