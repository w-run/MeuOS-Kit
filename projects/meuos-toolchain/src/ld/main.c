/* ld - MeuOS Toolchain static linker with multi-arch ELF output. */
#include "mt/ld.h"
#include "mt/target.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MT_LD_VERSION "0.2.0"
#define MAX_INPUTS 512
#define MAX_LIBPATHS 64

static void
usage(FILE *out)
{
	fprintf(out,
	        "usage: ld [-static] [-e entry] [-Ldir...] [-llib...] [--sysroot=dir]\n"
	        "          [--target=<arch>] -o output input.o [input.a ...]\n"
	        "       ld --help\n"
	        "       ld --version\n"
	        "supported targets: x86_64, i386, aarch64, riscv64, loongarch64\n"
	        "options:\n"
	        "  --target=<arch>  set target architecture (default: x86_64)\n"
	        "  -L<dir>          add library search path\n"
	        "  -l<lib>          link lib<lib>.a (searched in -L paths and sysroot)\n"
	        "  --sysroot=<dir>  set system root (adds <dir>/usr/lib to search path)\n"
	        "  -e <entry>       entry symbol (default: _start)\n"
	        "  -static          static link (default, ignored for compat)\n");
}

/* Search for lib<name>.a in search paths; return 0 on success. */
static int
find_library(const char *name, const char **libpaths, int libpath_count,
             char *result, size_t result_size)
{
	int i;
	for (i = 0; i < libpath_count; ++i) {
		struct stat st;
		snprintf(result, result_size, "%s/lib%s.a", libpaths[i], name);
		if (stat(result, &st) == 0 && S_ISREG(st.st_mode))
			return 0;
	}
	return -1;
}

int
main(int argc, char **argv)
{
	const char *output = "a.out";
	const char *entry = "_start";
	const char *target_name = NULL;
	const char *inputs[MAX_INPUTS];
	const char *libpaths[MAX_LIBPATHS];
	const char *sysroot = NULL;
	const char *error;
	int input_count = 0;
	int libpath_count = 0;
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
			printf("meuos-toolchain ld %s (%s)\n",
			       MT_LD_VERSION,
			       t ? t->name : "unknown");
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
		/* --target=<arch> */
		if (strncmp(argv[i], "--target=", 9) == 0) {
			const char *name = argv[i] + 9;
			if (strcmp(name, "x86_64-linux") == 0)
				name = "x86_64";
			if (!mt_target_lookup(name)) {
				fprintf(stderr, "ld: unsupported target: %s\n", name);
				return 2;
			}
			target_name = name;
			continue;
		}
		/* --sysroot=<dir> */
		if (strncmp(argv[i], "--sysroot=", 10) == 0) {
			sysroot = argv[i] + 10;
			continue;
		}
		/* --sysroot <dir> (space-separated) */
		if (strcmp(argv[i], "--sysroot") == 0) {
			if (++i >= argc) {
				usage(stderr);
				return 2;
			}
			sysroot = argv[i];
			continue;
		}
		/* -L<dir> or -L <dir> */
		if (argv[i][0] == '-' && argv[i][1] == 'L') {
			if (argv[i][2]) {
				if (libpath_count < MAX_LIBPATHS)
					libpaths[libpath_count++] = argv[i] + 2;
			} else {
				if (++i >= argc) {
					usage(stderr);
					return 2;
				}
				if (libpath_count < MAX_LIBPATHS)
					libpaths[libpath_count++] = argv[i];
			}
			continue;
		}
		/* -l<lib> or -l:<filename> */
		if (argv[i][0] == '-' && argv[i][1] == 'l' && argv[i][2]) {
			const char *libname = argv[i] + 2;
			char path[1024];
			const char *paths[MAX_LIBPATHS + 2];
			int npaths = 0;
			int j;
			int is_colon = (libname[0] == ':');
			if (is_colon)
				libname++;
			for (j = 0; j < libpath_count; ++j)
				paths[npaths++] = libpaths[j];
			if (sysroot) {
				static char sbuf[1024];
				snprintf(sbuf, sizeof(sbuf), "%s/usr/lib", sysroot);
				paths[npaths++] = sbuf;
			}
			if (is_colon) {
				/* -l:filename -> search exact filename */
				for (j = 0; j < npaths; ++j) {
					struct stat st;
					snprintf(path, sizeof(path), "%s/%s", paths[j], libname);
					if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
						break;
				}
				if (j >= npaths) {
					fprintf(stderr, "ld: cannot find -l:%s\n", libname);
					return 1;
				}
			} else if (find_library(libname, paths, npaths, path, sizeof(path)) != 0) {
				fprintf(stderr, "ld: cannot find -l%s\n", libname);
				return 1;
			}
			if (input_count >= MAX_INPUTS) {
				fprintf(stderr, "ld: too many input files\n");
				return 2;
			}
			{ char *dup = malloc(strlen(path)+1); strcpy(dup, path); inputs[input_count++] = dup; }
			continue;
		}
		if (argv[i][0] == '-') {
			fprintf(stderr, "ld: unsupported option: %s\n", argv[i]);
			return 2;
		}
		if (input_count >= MAX_INPUTS) {
			fprintf(stderr, "ld: too many input files\n");
			return 2;
		}
		inputs[input_count++] = argv[i];
	}
	if (input_count == 0) {
		usage(stderr);
		return 2;
	}
	if (mt_ld_link(output, entry, inputs, (size_t)input_count,
	               target_name, &error) != 0) {
		fprintf(stderr, "ld: %s\n", error ? error : "link failed");
		return 1;
	}
	return 0;
}
