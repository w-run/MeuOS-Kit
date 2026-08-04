/* ld - MeuOS Toolchain static linker with multi-arch ELF output. */
#include "mt/ld.h"
#include "mt/target.h"
#include "mt/msys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MT_LD_VERSION "0.2.0"
#define MAX_INPUTS 512
#define MAX_LIBPATHS 64

static void
usage(FILE *out)
{
	fprintf(out,
	        "usage: ld [-static] [-shared] [-soname name] [-e entry] [-Ldir...] [-llib...]\n"
	        "          [--sysroot=dir] [--target=<arch>] -o output input.o [input.a ...]\n"
	        "       ld --help\n"
	        "       ld --version\n"
	        "supported targets: x86_64, i386, aarch64, riscv64, loongarch64\n"
	        "options:\n"
	        "  --target=<arch>  set target architecture (default: x86_64)\n"
	        "  -shared          build shared library (ET_DYN)\n"
	        "  -soname <name>   set DT_SONAME for shared library\n"
	        "  -pie             build position-independent executable (ET_DYN)\n"
	        "  -L<dir>          add library search path\n"
	        "  -l<lib>          link lib<lib>.a (searched in -L paths and sysroot)\n"
	        "  --sysroot=<dir>  set system root (adds <dir>/usr/lib to search path)\n"
	        "  -e <entry>       entry symbol (default: _start)\n"
	        "  --build-id       generate .note.gnu.build-id (FNV-1a hash)\n"
	"  --defsym=SYM=VAL define symbol SYM to absolute value VAL\n"
	                    "                    (VAL may use 0x prefix for hexadecimal;\n"
	                    "                     may be repeated for multiple symbols)\n"
	                    "  --wrap=<symbol>  redirect references to SYMBOL to\n"
	                    "                    __wrap_SYMBOL (__real_SYMBOL accesses original)\n"
	                    "  -static          static link (default, ignored for compat)\n");
}

/* Forward declarations for msys VFS helpers (defined below). */
static int find_library_msys(const char *name, char *result, size_t result_size);
static int find_library_msys_exact(const char *name, char *result, size_t result_size);

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
	/* Fallback: search in .msys VFS handles */
	return find_library_msys(name, result, result_size);
}

/* ---- .msys single-file sysroot VFS support ---- */

/* Check if a path ends with ".msys". */
static int
msys_is_sysroot(const char *path)
{
	size_t len = path ? strlen(path) : 0;
	return len >= 5 && strcmp(path + len - 5, ".msys") == 0;
}

/* Global msys handles for VFS zero-extract mode. */
struct msys *ld_msys = NULL;
#define LD_MAX_MSYS_LIBS 16
static struct msys *ld_msys_libs[LD_MAX_MSYS_LIBS] = {NULL};
static int ld_msys_lib_count = 0;

/* Cleanup all msys handles at exit. */
static void
cleanup_msys_vfs(void)
{
	int i;
	if (ld_msys) {
		msys_close(ld_msys);
		ld_msys = NULL;
	}
	for (i = 0; i < ld_msys_lib_count; ++i) {
		if (ld_msys_libs[i]) {
			msys_close(ld_msys_libs[i]);
			ld_msys_libs[i] = NULL;
		}
	}
}

/* Try to find a library inside all available msys handles.
 * Returns 0 with result set to "@msys:ID:internal_path" on success. */
static int
find_library_msys(const char *name, char *result, size_t result_size)
{
	size_t sz;

	/* Primary sysroot msys handle */
	if (ld_msys) {
		/* Write directly to result to avoid -Wformat-truncation false positive.
		 * Build: "@msys:0:usr/lib/lib<name>.a" */
		snprintf(result, result_size, "@msys:0:usr/lib/lib%s.a", name);
		if (msys_search(ld_msys, result + 8, &sz))
			return 0;
	}
	/* Extra -L .msys handles */
	for (int i = 0; i < ld_msys_lib_count; ++i) {
		if (!ld_msys_libs[i]) continue;
		snprintf(result, result_size, "@msys:%d:usr/lib/lib%s.a", i + 1, name);
		/* Skip past "@msys:N:" to get the internal path for search */
		const char *internal = strchr(result + 6, ':') + 1;
		if (internal && msys_search(ld_msys_libs[i], internal, &sz))
			return 0;
	}
	return -1;
}

/* Try to find an exact filename inside all msys handles.
 * Constructs "usr/lib/<name>" since that's how search paths map to .msys. */
static int
find_library_msys_exact(const char *name, char *result, size_t result_size)
{
	size_t sz;

	if (ld_msys) {
		snprintf(result, result_size, "@msys:0:usr/lib/%s", name);
		if (msys_search(ld_msys, result + 8, &sz))
			return 0;
	}
	for (int i = 0; i < ld_msys_lib_count; ++i) {
		if (!ld_msys_libs[i]) continue;
		snprintf(result, result_size, "@msys:%d:usr/lib/%s", i + 1, name);
		const char *internal = strchr(result + 6, ':') + 1;
		if (internal && msys_search(ld_msys_libs[i], internal, &sz))
			return 0;
	}
	return -1;
}

/* Resolve a @msys:HANDLE_ID:internal_path reference.
 * Returns bytes loaded on success (buf is malloc'd), or -1 on error (errno set). */
int
msys_vfs_load(const char *path, void **buf, size_t *size)
{
	int id;
	const char *internal;
	struct msys *handle;

	if (strncmp(path, "@msys:", 6) != 0)
		return -1;

	id = atoi(path + 6);
	internal = strchr(path + 6, ':');
	if (!internal)
		return -1;
	internal++;

	if (id == 0)
		handle = ld_msys;
	else if (id >= 1 && id <= ld_msys_lib_count)
		handle = ld_msys_libs[id - 1];
	else
		return -1;

	if (!handle)
		return -1;

	return msys_load(handle, internal, buf, size);
}

int
main(int argc, char **argv)
{
	const char *output = "a.out";
	const char *entry = "_start";
	const char *target_name = NULL;
	const char *soname = NULL;
	const char *dynamic_linker = NULL;
	int shared = 0;
	int pie = 0;
	int build_id = 0;
	int eh_frame_hdr = 0;
	int as_needed = -1; /* default: unspecified */
	int whole_archive = 0;
	int in_start_group = 0;
	(void)in_start_group;
	int no_undefined = 0;
	int gc_sections = 0;
	int print_map = 0;
	int cref = 0;
	const char *link_script = NULL;
	const char *version_script = NULL;
	const char *defsym_list[64];
	size_t defsym_count = 0;
	const char *wrap_list[64];
	size_t wrap_count = 0;
	const char *needed_list[64];
	size_t needed_count = 0;
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
		if (strcmp(argv[i], "-shared") == 0 || strcmp(argv[i], "--shared") == 0) {
			shared = 1;
			continue;
		}
		if (strcmp(argv[i], "-pie") == 0 || strcmp(argv[i], "--pie") == 0) {
			pie = 1;
			continue;
		}
		if (strcmp(argv[i], "-no-pie") == 0 || strcmp(argv[i], "--no-pie") == 0) {
			pie = 0;
			continue;
		}
		if (strcmp(argv[i], "--build-id") == 0 || strcmp(argv[i], "-build-id") == 0) {
			build_id = 1;
			continue;
		}
		if (strcmp(argv[i], "--no-build-id") == 0) {
			build_id = 0;
			continue;
		}
		if (strcmp(argv[i], "--eh-frame-hdr") == 0 || strcmp(argv[i], "-eh-frame-hdr") == 0) {
			eh_frame_hdr = 1;
			continue;
		}
		if (strcmp(argv[i], "--as-needed") == 0) {
			as_needed = 1;
			continue;
		}
		if (strcmp(argv[i], "--no-as-needed") == 0) {
			as_needed = 0;
			continue;
		}
		if (strcmp(argv[i], "--whole-archive") == 0) {
			whole_archive = 1;
			continue;
		}
		if (strcmp(argv[i], "--no-whole-archive") == 0) {
			whole_archive = 0;
			continue;
		}
		if (strcmp(argv[i], "--start-group") == 0) {
			in_start_group = 1;
			continue;
		}
		if (strcmp(argv[i], "--end-group") == 0) {
			in_start_group = 0;
			continue;
		}
		if (strcmp(argv[i], "--no-undefined") == 0 ||
		    strcmp(argv[i], "-z") == 0) {
			no_undefined = 1;
			if (strcmp(argv[i], "-z") == 0) {
				/* -z defs / -z undefs */
				if (++i >= argc) { usage(stderr); return 2; }
				if (strcmp(argv[i], "defs") == 0 ||
				    strcmp(argv[i], "undefs") == 0)
					no_undefined = 1;
				else if (strcmp(argv[i], "nodefs") == 0 ||
				         strcmp(argv[i], "noundefs") == 0)
					no_undefined = 0;
			}
			continue;
		}
		if (strcmp(argv[i], "--gc-sections") == 0) {
			gc_sections = 1;
			continue;
		}
		if (strcmp(argv[i], "--no-gc-sections") == 0) {
			gc_sections = 0;
			continue;
		}
		if (strcmp(argv[i], "--print-map") == 0 || strcmp(argv[i], "-Map") == 0) {
			print_map = 1;
			continue;
		}
		if (strcmp(argv[i], "--cref") == 0) {
			cref = 1;
			continue;
		}
		if (strncmp(argv[i], "--defsym=", 9) == 0) {
			if (defsym_count < 64)
				defsym_list[defsym_count++] = argv[i] + 9;
			continue;
		}
		if (strncmp(argv[i], "--wrap=", 7) == 0) {
			if (wrap_count < 64)
				wrap_list[wrap_count++] = argv[i] + 7;
			continue;
		}
		if (strncmp(argv[i], "--add-needed=", 13) == 0) {
			if (needed_count < 64)
				needed_list[needed_count++] = argv[i] + 13;
			continue;
		}
		if (strcmp(argv[i], "-T") == 0) {
			if (++i >= argc) { usage(stderr); return 2; }
			link_script = argv[i];
			continue;
		}
		if (strcmp(argv[i], "--link-script") == 0) {
			if (++i >= argc) { usage(stderr); return 2; }
			link_script = argv[i];
			continue;
		}
		if (strncmp(argv[i], "--version-script=", 17) == 0) {
			version_script = argv[i] + 17;
			continue;
		}
		if (strcmp(argv[i], "-soname") == 0) {
			if (++i >= argc) { usage(stderr); return 2; }
			soname = argv[i];
			continue;
		}
		if (strcmp(argv[i], "-dynamic-linker") == 0 ||
		    strcmp(argv[i], "-I") == 0) {
			if (++i >= argc) { usage(stderr); return 2; }
			dynamic_linker = argv[i];
			continue;
		}
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
			if (msys_is_sysroot(sysroot)) {
				ld_msys = msys_open(sysroot);
				if (!ld_msys) {
					fprintf(stderr, "ld: failed to open .msys sysroot: %s\n",
					        sysroot);
					return 2;
				}
				/* Keep sysroot pointing to .msys path -- no extraction.
				 * read_file() will fall back to msys_load when fopen fails. */
				atexit(cleanup_msys_vfs);
			}
			continue;
		}
		/* --sysroot <dir> (space-separated) */
		if (strcmp(argv[i], "--sysroot") == 0) {
			if (++i >= argc) {
				usage(stderr);
				return 2;
			}
			sysroot = argv[i];
			if (msys_is_sysroot(sysroot)) {
				ld_msys = msys_open(sysroot);
				if (!ld_msys) {
					fprintf(stderr, "ld: failed to open .msys sysroot: %s\n",
					        sysroot);
					return 2;
				}
			}
			continue;
		}
		/* -L<dir> or -L <dir> */
		if (argv[i][0] == '-' && argv[i][1] == 'L') {
			const char *Lpath;
			if (argv[i][2]) {
				Lpath = argv[i] + 2;
			} else {
				if (++i >= argc) {
					usage(stderr);
					return 2;
				}
				Lpath = argv[i];
			}
			/* If -L points to a .msys file, open it as a VFS handle */
			if (msys_is_sysroot(Lpath)) {
				if (ld_msys_lib_count < LD_MAX_MSYS_LIBS) {
					struct msys *m = msys_open(Lpath);
					if (!m) {
						fprintf(stderr, "ld: failed to open -L .msys: %s\n", Lpath);
						return 2;
					}
					ld_msys_libs[ld_msys_lib_count++] = m;
				} else {
					fprintf(stderr, "ld: too many -L .msys files\n");
					return 2;
				}
			} else if (libpath_count < MAX_LIBPATHS) {
				libpaths[libpath_count++] = Lpath;
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
					/* Fallback: search in .msys VFS handles */
					if (find_library_msys_exact(libname, path, sizeof(path)) != 0) {
						fprintf(stderr, "ld: cannot find -l:%s\n", libname);
						return 1;
					}
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
	struct mt_ld_options opts;
	memset(&opts, 0, sizeof(opts));
	opts.output  = output;
	opts.entry   = shared ? NULL : entry;
	opts.soname  = soname;
	opts.dynamic_linker = dynamic_linker;
	opts.shared  = shared;
	opts.pie     = pie;
	opts.build_id    = build_id;
	opts.eh_frame_hdr = eh_frame_hdr;
	opts.as_needed   = as_needed;
	opts.whole_archive = whole_archive;
	opts.no_undefined = no_undefined;
	opts.gc_sections = gc_sections;
	opts.print_map = print_map;
	opts.cref = cref;
	opts.link_script = link_script;
	opts.version_script = version_script;
	opts.defsym = defsym_count ? defsym_list : NULL;
	opts.defsym_count = defsym_count;
	opts.wrap = wrap_count ? wrap_list : NULL;
	opts.wrap_count = wrap_count;
	opts.add_needed = needed_count ? needed_list : NULL;
	opts.add_needed_count = needed_count;
	if (mt_ld_link_opts(&opts, inputs, (size_t)input_count,
	                    target_name, &error) != 0) {
		fprintf(stderr, "ld: %s\n", error ? error : "link failed");
		return 1;
	}
	return 0;
}
