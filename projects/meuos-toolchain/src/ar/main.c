/* ar - initial MeuOS Toolchain archive utility. */
#include "mt/archive.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define MT_AR_VERSION "0.2.0"

static void
usage(FILE *output)
{
	fprintf(output,
	        "usage: ar [rcsq] archive [member ...]\n"
	        "       ar [tpx] archive [member ...]\n"
	        "       ar --help\n"
	        "       ar --version\n\n"
	        "operations: r replace, q append, c create, s index,\n"
	        "           t list, p print, x extract\n"
	        "format: reproducible SysV/GNU archive with symbol index\n");
}

static int
list_output(const struct mt_ar_member *member, void *context)
{
	(void)context;
	printf("%s\n", member->name);
	return 0;
}

static int
has_operation(const char *options, char operation)
{
	return strchr(options, operation) != NULL;
}

static int
options_valid(const char *options)
{
	size_t i;
	for (i = 0; options[i] != '\0'; ++i) {
		if (!strchr("rcqstpx", options[i]))
			return 0;
	}
	return options[0] != '\0';
}

static void
report_error(const char *archive)
{
	fprintf(stderr, "ar: %s: %s\n", archive, strerror(errno));
}

int
main(int argc, char **argv)
{
	const char *options;
	const char *archive;
	const char *const *members;
	size_t member_count;
	int create;
	int list;
	int print;
	int extract;

	if (argc == 2 && strcmp(argv[1], "--help") == 0) {
		usage(stdout);
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "--version") == 0) {
		printf("meuos-toolchain ar %s (x86_64 bootstrap)\n", MT_AR_VERSION);
		return 0;
	}
	if (argc < 3) {
		usage(stderr);
		return 2;
	}
	options = argv[1];
	if (options[0] == '-')
		++options;
	if (!options_valid(options)) {
		fprintf(stderr, "ar: unsupported operation flags: %s\n", argv[1]);
		return 2;
	}
	archive = argv[2];
	members = (const char *const *)&argv[3];
	member_count = argc > 3 ? (size_t)(argc - 3) : 0;
	create = has_operation(options, 'r') || has_operation(options, 'q') ||
	         has_operation(options, 'c');
	list = has_operation(options, 't');
	print = has_operation(options, 'p');
	extract = has_operation(options, 'x');
	if ((create + list + print + extract) != 1) {
		fprintf(stderr, "ar: exactly one operation is required\n");
		return 2;
	}
	if (create) {
		if (member_count == 0 && !has_operation(options, 'c')) {
			fprintf(stderr, "ar: no archive members specified\n");
			return 2;
		}
		if (mt_ar_update(archive, members, member_count,
		                 has_operation(options, 'q') ? MT_AR_UPDATE_APPEND :
		                 MT_AR_UPDATE_REPLACE) != 0) {
			report_error(archive);
			return 1;
		}
		return 0;
	}
	if (list) {
		if (mt_ar_list(archive, list_output, NULL) != 0) {
			report_error(archive);
			return 1;
		}
		return 0;
	}
	if (print) {
		if (mt_ar_print(archive, members, member_count, stdout) != 0) {
			report_error(archive);
			return 1;
		}
		return 0;
	}
	if (extract) {
		if (mt_ar_extract(archive, members, member_count) != 0) {
			report_error(archive);
			return 1;
		}
		return 0;
	}
	return 2;
}
