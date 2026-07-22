/* ranlib - generate or update archive symbol index. */
#include "mt/archive.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define MT_RANLIB_VERSION "0.2.0"

static void
usage(FILE *out)
{
	fprintf(out,
	        "usage: ranlib [--help] [--version] archive...\n"
	        "  Generates/updates the symbol index of each archive.\n"
	        "  Equivalent to: ar s <archive>\n");
}

int
main(int argc, char **argv)
{
	int i;
	if (argc >= 2 && strcmp(argv[1], "--help") == 0) {
		usage(stdout);
		return 0;
	}
	if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
		printf("meuos-toolchain ranlib %s (x86_64 bootstrap)\n",
		       MT_RANLIB_VERSION);
		return 0;
	}
	if (argc < 2) {
		usage(stderr);
		return 2;
	}
	for (i = 1; i < argc; ++i) {
		if (mt_ar_update(argv[i], NULL, 0, MT_AR_UPDATE_REPLACE) != 0) {
			fprintf(stderr, "ranlib: %s: %s\n", argv[i], strerror(errno));
			return 1;
		}
	}
	return 0;
}
