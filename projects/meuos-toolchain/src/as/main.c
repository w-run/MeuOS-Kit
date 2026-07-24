/* as - MeuOS Toolchain assembler with multi-arch ELF output. */
#include "mt/as.h"
#include "mt/target.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MT_AS_VERSION "0.1.0"

static void
usage(FILE *out)
{
	fprintf(out,
	        "usage: as [-o output] [--target=<arch>] input.s\n"
	        "       as --help\n"
	        "       as --version\n"
	        "supported targets: x86_64, i386, aarch64, riscv64, loongarch64\n");
}

int
main(int argc, char **argv)
{
	const char *input = NULL;
	const char *output = "a.out";
	const char *target_name = NULL;
	const char *error;
	unsigned line;
	int i;

	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--help") == 0) {
			usage(stdout);
			return 0;
		}
		if (strcmp(argv[i], "--version") == 0) {
			const struct mt_target *t = target_name
				? mt_target_lookup(target_name)
				: &mt_target_x86_64;
			printf("meuos-toolchain as %s (%s)\n",
			       MT_AS_VERSION,
			       t ? t->name : "unknown");
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
			const char *name = argv[i] + 9;
			if (strcmp(name, "x86_64-linux") == 0)
				name = "x86_64";
			if (!mt_target_lookup(name)) {
				fprintf(stderr, "as: unsupported target: %s\n", name);
				return 2;
			}
			target_name = name;
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
	if (mt_as_assemble(input, output, target_name, &error, &line) != 0) {
		fprintf(stderr, "as: %s:%u: %s\n", input, line, error);
		return 1;
	}
	return 0;
}
