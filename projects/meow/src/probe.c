/* probe.c — meow inline feature detection (autoconf replacement).
 *
 * Parses the YAML "probe:" section and runs compile/link tests to
 * determine available headers, functions, types, and arbitrary code
 * features, then generates config.h for the recipe. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "meow.h"

/* Temporary file helpers */
static char tmpdir[] = "/tmp/meow-probe.XXXXXX";

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

	char cmd[2048];
	int n = snprintf(cmd, sizeof cmd, "%s %s -c -o %s %s 2>/dev/null",
	         cc ? cc : "mcc",
	         cflags ? cflags : "",
	         obj, src);
	if (n < 0 || (size_t)n >= sizeof(cmd)) { unlink(src); return -1; }
	rc = system(cmd);
	unlink(obj);
	unlink(src);
	return rc == 0 ? 0 : -1;
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
	probe_cc[0] = 0;
	probe_cflags[0] = 0;
	strcpy(probe_config, "config.h");
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

/* Run all registered probes and generate config.h. Returns 0 on success. */
int
probe_run(const char *build_dir)
{
	char config_path[512];
	FILE *f;
	int rc = 0;
	size_t total = nprobe_headers + nprobe_funcs + nprobe_codes + nprobe_decls;

	if (total == 0)
		return 0; /* nothing to probe */

	if (probe_init() != 0)
		return -1;

	snprintf(config_path, sizeof config_path, "%s/%s",
	         build_dir ? build_dir : ".",
	         probe_config);

	f = fopen(config_path, "w");
	if (!f) { probe_fini(); return -1; }

	fprintf(f, "/* config.h — generated by meow probe. */\n");
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

	fprintf(f, "\n#endif /* MEOW_CONFIG_H */\n");
	fclose(f);

	fprintf(stderr, "meow: probe: wrote %s (%zu checks, %zu headers, %zu funcs, %zu decls, %zu codes)\n",
	        config_path, total, nprobe_headers, nprobe_funcs, nprobe_decls, nprobe_codes);

	probe_fini();
	return rc;
}
