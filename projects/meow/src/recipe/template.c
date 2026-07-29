/* meow template — config.h.in autoconf-style template processor.
 *
 * Usage: meow template [-DNAME=VALUE]... input.tmpl [output]
 *
 * Processes autoconf-style config.h.in templates:
 *   - Replaces @VAR@ placeholders with values (-D wins over environ)
 *   - Converts `#undef VAR` to `#define VAR 1` if VAR is defined
 *     or comments it out (undef commented) if not
 *   - Leaves unrecognised @VAR@ untouched
 */
#define _POSIX_C_SOURCE 200809L
#include "meow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define MAX_DEFINES 256

struct define {
	char *name;
	char *value;
};

static struct define defines[MAX_DEFINES];
static size_t ndefines;

/* Add / update a definition from -DNAME=VALUE (or -DNAME, implies =1). */
static int
add_define(const char *arg)
{
	if (ndefines >= MAX_DEFINES) {
		meow_msg(MSG_ERROR, "too many -D definitions (max %d)",
		         MAX_DEFINES);
		return -1;
	}
	const char *eq = strchr(arg, '=');
	if (eq) {
		defines[ndefines].name = strndup(arg, (size_t)(eq - arg));
		defines[ndefines].value = strdup(eq + 1);
	} else {
		defines[ndefines].name = strdup(arg);
		defines[ndefines].value = strdup("1");
	}
	if (!defines[ndefines].name || !defines[ndefines].value) {
		meow_msg(MSG_ERROR, "out of memory");
		return -1;
	}
	ndefines++;
	return 0;
}

/* Look up a variable: -D table first, then environment. */
static const char *
lookup_var(const char *name)
{
	for (size_t i = 0; i < ndefines; i++)
		if (strcmp(defines[i].name, name) == 0)
			return defines[i].value;
	return getenv(name);
}

static int
is_defined(const char *name)
{
	return lookup_var(name) != NULL;
}

/* Write the processed form of one template line to `out`. */
static void
process_line(FILE *out, const char *line)
{
	size_t len = strlen(line);
	while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
		len--;

	/* --- #undef VAR handling --- */
	if (len > 7 && strncmp(line, "#undef ", 7) == 0) {
		const char *vn = line + 7;
		while (*vn == ' ' || *vn == '\t')
			vn++;
		const char *end = vn;
		size_t remaining = len - (size_t)(vn - line);
		while (remaining > 0 && *end != ' ' && *end != '\t') {
			end++;
			remaining--;
		}
		size_t varlen = (size_t)(end - vn);
		char name[256];
		if (varlen >= sizeof(name))
			varlen = sizeof(name) - 1;
		memcpy(name, vn, varlen);
		name[varlen] = '\0';

		if (is_defined(name)) {
			const char *val = lookup_var(name);
			if (strcmp(val, "1") == 0)
				fprintf(out, "#define %s\n", name);
			else
				fprintf(out, "#define %s %s\n", name, val);
		} else {
			fprintf(out, "/* #undef %s */\n", name);
		}
		return;
	}

	/* --- General @VAR@ substitution --- */
	size_t pos = 0;
	while (pos < len) {
		const char *atsign = memchr(line + pos, '@', len - pos);
		if (!atsign) {
			fwrite(line + pos, 1, len - pos, out);
			fputc('\n', out);
			return;
		}

		/* Emit text before this @ */
		size_t pre = (size_t)(atsign - (line + pos));
		if (pre > 0)
			fwrite(line + pos, 1, pre, out);

		size_t var_start = (size_t)(atsign - line) + 1;
		if (var_start >= len) {
			fputc('@', out);
			fputc('\n', out);
			return;
		}

		const char *close_at = memchr(line + var_start, '@',
		                              len - var_start);
		if (!close_at) {
			fputc('@', out);
			fwrite(line + var_start, 1, len - var_start, out);
			fputc('\n', out);
			return;
		}

		/* Variable name between the two @s */
		size_t varlen = (size_t)(close_at - (line + var_start));
		char varname[256];
		if (varlen >= sizeof(varname))
			varlen = sizeof(varname) - 1;
		memcpy(varname, line + var_start, varlen);
		varname[varlen] = '\0';

		const char *val = lookup_var(varname);
		if (val)
			fputs(val, out);
		else {
			/* Not found — keep the original placeholder */
			fputc('@', out);
			fwrite(line + var_start, 1, varlen, out);
			fputc('@', out);
		}
		pos = (size_t)(close_at - line) + 1;
	}

	fputc('\n', out);
}

int
cmd_template(int argc, char **argv)
{
	const char *input_file = NULL;
	const char *output_file = NULL;
	int i = 0;

	/* Parse -D flags, then positional input [output]. */
	while (i < argc) {
		if (strncmp(argv[i], "-D", 2) == 0) {
			if (add_define(argv[i] + 2) != 0)
				return 1;
			i++;
		} else if (!input_file) {
			input_file = argv[i];
			i++;
		} else if (!output_file) {
			output_file = argv[i];
			i++;
		} else {
			meow_msg(MSG_ERROR, "unexpected argument: %s", argv[i]);
			return 1;
		}
	}

	if (!input_file) {
		meow_msg(MSG_ERROR,
		         "usage: meow template [-DNAME=VALUE]... input.tmpl [output]");
		return 1;
	}

	FILE *in = fopen(input_file, "r");
	if (!in) {
		meow_msg(MSG_ERROR, "cannot open input: %s: %s",
		         input_file, strerror(errno));
		return 1;
	}

	FILE *out = stdout;
	if (output_file) {
		out = fopen(output_file, "w");
		if (!out) {
			meow_msg(MSG_ERROR, "cannot open output: %s: %s",
			         output_file, strerror(errno));
			fclose(in);
			return 1;
		}
	}

	char line[4096];
	while (fgets(line, sizeof(line), in))
		process_line(out, line);

	fclose(in);
	if (output_file)
		fclose(out);

	return 0;
}
