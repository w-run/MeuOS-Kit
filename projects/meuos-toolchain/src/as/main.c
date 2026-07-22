/* as - x86_64-first MeuOS Toolchain assembler. */
#include "mt/as.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MT_AS_VERSION "0.1.0"

static void
usage(FILE *out)
{
	fprintf(out,
	        "usage: as [-o output] [--target=x86_64] input.s\n"
	        "       as --help\n"
	        "       as --version\n");
}

int
main(int argc, char **argv)
{
	const char *input = NULL;
	const char *output = "a.out";
	const char *error;
	unsigned line;
	int i;

	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--help") == 0) {
			usage(stdout);
			return 0;
		}
		if (strcmp(argv[i], "--version") == 0) {
			printf("meuos-toolchain as %s (x86_64)\n", MT_AS_VERSION);
			return 0;
		}
		if (strcmp(argv[i], "-o") == 0) {
			if (++i >= argc) {
				usage(stderr);
				return 2;
			}
			output = argv[i];
			continue;
		}
		if (strncmp(argv[i], "--target=", 9) == 0) {
			if (strcmp(argv[i] + 9, "x86_64") != 0 &&
			    strcmp(argv[i] + 9, "x86_64-linux") != 0) {
				fprintf(stderr, "as: only x86_64 is implemented\n");
				return 2;
			}
			continue;
		}
		if (argv[i][0] == '-' && argv[i][1] == 'o' && argv[i][2] != '\0') {
			output = argv[i] + 2;
			continue;
		}
		if (argv[i][0] == '-') {
			fprintf(stderr, "as: unsupported option: %s\n", argv[i]);
			return 2;
		}
		if (input) {
			fprintf(stderr, "as: multiple input files are not supported\n");
			return 2;
		}
		input = argv[i];
	}
	if (!input) {
		usage(stderr);
		return 2;
	}
	if (mt_as_assemble(input, output, &error, &line) != 0) {
		fprintf(stderr, "as: %s:%u: %s\n", input, line, error);
		return 1;
	}
	return 0;
}
