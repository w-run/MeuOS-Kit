/* ld - x86_64 static MeuOS Toolchain linker. */
#include "mt/ld.h"

#include <stdio.h>
#include <string.h>

#define MT_LD_VERSION "0.1.0"

static void
usage(FILE *out)
{
	fprintf(out,
	        "usage: ld [-static] [-e entry] -o output input.o [input.a ...]\n"
	        "       ld --help\n"
	        "       ld --version\n");
}

int
main(int argc, char **argv)
{
	const char *output = "a.out";
	const char *entry = "_start";
	const char *inputs[256];
	const char *error;
	int input_count = 0;
	int i;

	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--help") == 0) {
			usage(stdout);
			return 0;
		}
		if (strcmp(argv[i], "--version") == 0) {
			printf("meuos-toolchain ld %s (x86_64 static)\n", MT_LD_VERSION);
			return 0;
		}
		if (strcmp(argv[i], "-static") == 0 || strcmp(argv[i], "--static") == 0)
			continue;
		if (strcmp(argv[i], "-o") == 0) {
			if (++i >= argc) {
				usage(stderr);
				return 2;
			}
			output = argv[i];
			continue;
		}
		if (argv[i][0] == '-' && argv[i][1] == 'o' && argv[i][2]) {
			output = argv[i] + 2;
			continue;
		}
		if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--entry") == 0) {
			if (++i >= argc) {
				usage(stderr);
				return 2;
			}
			entry = argv[i];
			continue;
		}
		if (strncmp(argv[i], "--entry=", 8) == 0) {
			entry = argv[i] + 8;
			continue;
		}
		if (argv[i][0] == '-') {
			fprintf(stderr, "ld: unsupported option: %s\n", argv[i]);
			return 2;
		}
		if (input_count == (int)(sizeof(inputs) / sizeof(inputs[0]))) {
			fprintf(stderr, "ld: too many input files\n");
			return 2;
		}
		inputs[input_count++] = argv[i];
	}
	if (input_count == 0) {
		usage(stderr);
		return 2;
	}
	if (mt_ld_link(output, entry, inputs, (size_t)input_count, &error) != 0) {
		fprintf(stderr, "ld: %s\n", error ? error : "link failed");
		return 1;
	}
	return 0;
}
