/*
 * mcc - MeuOS C Compiler
 *
 * Unified driver for the source-level integration of the frontend
 * (lex -> parse -> sema -> AST) and the IR backend (IR construction ->
 * optimization passes -> register allocation -> asm emission).
 *
 * Design:
 *   - In -E mode, preprocessed tokens are written to stdout (or -o file).
 *   - In -S mode, asm is written directly to stdout (or -o file).
 *   - In -c mode, asm is written to a temp file, then the host assembler
 *     (`cc -c`) is invoked to produce a .o (no linking).
 *   - In default mode, asm is written to a temp file, then the host
 *     assembler+linker (cc) is invoked to produce the final executable.
 *     The host-cc handoff is a Phase 1a bootstrap convenience - later
 *     phases replace `cc` with a self-hosted path.
 */

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"
#include "arg.h"
#include "mcc.h"
#include "driver_internal.h"

extern int emit_debug;  /* from emit/emit.c */
extern void emitdbgfile(char *, FILE *);  /* from emit/emit.c */

/* Global msys handle and path exposed to the preprocessor (pp.c) for
 * VFS-based include file reading (msys_fopen fallback). */
struct msys *msys_sysroot_handle;
const char *msys_sysroot_path;

/* Global IR backend state (declared extern in ir.h).
 * Per-arch Target objects are declared extern in driver_internal.h. */
Target T;
char debug['Z' + 1];
int opt_level = 2;    /* -O2 default */
/* warn_level and warn_as_error are defined in src/lex/token.c */
enum tls_model tls_model = TLSM_DEFAULT;

/* ARM sub-architecture flags (set via -march/-mcpu/-mfpu/-mfloat-abi) */
static const char *arm_march  = "armv7-a";
static const char *arm_mcpu   = NULL;
static const char *arm_mfpu   = "vfpv3-d16";
static const char *arm_mfloat_abi = "hard";

/* Global target features bitmask (MT_FEATURE_*), set by -march=native or
 * -march=x86-64-vN. 0 = baseline only. Used by backend emit to gate ISA
 * levels (second phase); for now it records the user's intent. */
uint64_t g_target_features = 0;
/* Set when -march=native is requested; resolved after target selection. */
int g_march_native_requested = 0;

/* Parse ARM architecture version from -march string (e.g., "armv7-a" -> 7) */
static int arm_arch_from_march(const char *m) {
	if (!m) return 7;
	if (strncmp(m, "armv", 4) == 0) return atoi(m + 4);
	if (strncmp(m, "v", 1) == 0) return atoi(m + 1);
	return 7;
}
/* Set ARM-specific predefines based on -march/-mcpu/-mfpu/-mfloat-abi flags */
static void arm_set_target_defines(void) {
	int arch_ver = arm_arch_from_march(arm_march ? arm_march : arm_mcpu);
	ppdefine("__arm__", "1");
	ppdefine("__ARM_ARCH", "");
	char *arch_num = malloc(16);
	snprintf(arch_num, 16, "%d", arch_ver);
	ppdefine("__ARM_ARCH_7__", "1");
	if (arch_ver >= 7) {
		ppdefine("__ARM_ARCH_7__", "1");
		ppdefine("__ARM_ARCH_7A__", "1");
	}
	ppdefine("__ARMEL__", "1");
	ppdefine("__ARM_EABI__", "1");
	if (arm_mfloat_abi && strcmp(arm_mfloat_abi, "hard") == 0)
		ppdefine("__ARM_PCS_VFP", "1");
	if (arm_mfpu) {
		ppdefine("__VFP_FP__", "1");
		if (strstr(arm_mfpu, "vfpv3") || strstr(arm_mfpu, "vfpv4")
		 || strstr(arm_mfpu, "neon"))
			ppdefine("__ARM_VFPV3__", "1");
	}
	ppdefine("__ILP32__", "1");
	ppdefine("_ILP32", "1");
	free(arch_num);
}

int
main(int argc, char *argv[])
{
	struct array inputs = {0}, incdirs = {0}, libdirs = {0}, libs = {0};
	struct array defines = {0}, undefs = {0};
	bool pponly = false, emit_asm_only = false, compile_only = false;
	bool verbose = false, nostdinc = false, nostdlib = false, nodefaultlibs = false;
	bool static_link = false, shared = false, pic = false, pie = false;
	bool meuos_specs = false, meuos_specs_host = false;
	char *output = NULL, *target = NULL, *first_input = NULL, *sysroot = NULL;
	struct msys *msys_handle = NULL;
	int depmode = 0;       /* 0 none, 1 -M, 2 -MM, 3 -MD, 4 -MMD */
	char *depfile = NULL;
	int i;

	/* Normalize argv: gcc/clang compatibility allows single-dash
	 * multi-letter options (-static, -shared, ...) which we rewrite
	 * to the standard double-dash form (--static, --shared, ...).
	 * Single-letter short options and category-prefixed options
	 * (-W<w>, -f<f>, -m<m>) are left untouched. See arg_compat.c. */
	argv = arg_normalize(argc, argv);
	argv0 = progname(argv[0], "mcc");

	/* Fetch an option argument that may be attached (-Dfoo) or separated
	 * (-D foo). `attached` points just past the option char(s). */
#define ARGVAL(attached) \
	(*(attached) ? (attached) : (i + 1 < argc ? argv[++i] : (usage(), (char *)0)))

	for (i = 1; i < argc; i++) {
		char *a = argv[i];

		if (a[0] != '-' || a[1] == '\0') {
			/* input file (or "-" for stdin) */
			arrayaddptr(&inputs, a);
			continue;
		}
		/* "--" terminates option parsing */
		if (a[0] == '-' && a[1] == '-' && a[2] == '\0') {
			while (++i < argc)
				arrayaddptr(&inputs, argv[i]);
			break;
		}
		/* --version / --help: true long-only options */
		if (strcmp(a, "--version") == 0) { print_version(); exit(0); }
		if (strcmp(a, "--help") == 0) { usage_long(); exit(0); }

		/* whole-arg multi-letter options (double-dash standard form;
		 * single-dash gcc forms are normalized by arg_normalize()).
		 * Category-prefixed options (-W<w>, -f<f>, -m<m>, -M<d>) stay
		 * single-dash and are handled below. */
	if (strncmp(a, "--std=", 6) == 0) {
		/* recorded: mcc is C11-flavoured regardless */
		continue;
	}
	if (strcmp(a, "--static") == 0) { static_link = true; continue; }
	if (strcmp(a, "--shared") == 0) { shared = true; pic = true; continue; }
	if (strcmp(a, "--nostdinc") == 0) { nostdinc = true; continue; }
	if (strcmp(a, "--nostdlib") == 0) { nostdlib = true; continue; }
	if (strcmp(a, "--nodefaultlibs") == 0) { nodefaultlibs = true; continue; }
	if (strcmp(a, "--pedantic") == 0 || strcmp(a, "--pedantic-errors") == 0)
		continue;
	if (strcmp(a, "--pipe") == 0) continue;
	if (strcmp(a, "--pie") == 0) { pie = true; continue; }
	/* -fPIE/-fpie/-fPIC/-fpic: -f is a category prefix, stays single-dash */
	if (strcmp(a, "-fPIE") == 0 || strcmp(a, "-fpie") == 0 ||
	    strcmp(a, "-fPIC") == 0 || strcmp(a, "-fpic") == 0)
		{ pic = true; continue; }
	/* -Wall/-Wextra/-Werror: -W is a category prefix, stays single-dash */
	if (strcmp(a, "-Wall") == 0 || strcmp(a, "-Wextra") == 0) {
		warn_level = WARN_ALL;
		continue;
	}
	if (strcmp(a, "-Werror") == 0) {
		warn_level |= WARN_ERROR;
		warn_as_error = true;
		continue;
	}
	/* --warn=<group>: 现代化警告体系（p9-ui 设计，不用 GCC -Wall 无语义命名）。
	 * 语义组：all / portable / style / performance / pedantic。
	 * 保留 -Wall 作为 --warn=all 的兼容别名。 */
	if (strncmp(a, "--warn=", 7) == 0) {
		const char *grp = a + 7;
		if (strcmp(grp, "all") == 0)
			warn_level = WARN_ALL;
		else if (strcmp(grp, "portable") == 0)
			warn_level = WARN_PORTABLE;
		else if (strcmp(grp, "style") == 0)
			warn_level = WARN_STYLE;
		else if (strcmp(grp, "performance") == 0)
			warn_level = WARN_PERFORMANCE;
		else if (strcmp(grp, "pedantic") == 0)
			warn_level = WARN_ALL | WARN_PEDANTIC;
		else
			fatal("unknown warning group '%s' (use all/portable/style/performance/pedantic)", grp);
		continue;
	}
	if (a[1] == 'W') continue;   /* other -Wxxx warning flags */
	if (strncmp(a, "-ftls-model=", 12) == 0) {
		const char *val = a + 12;
		if (strcmp(val, "global-dynamic") == 0)
			tls_model = TLSM_GLOBAL_DYNAMIC;
		else if (strcmp(val, "initial-exec") == 0)
			tls_model = TLSM_INITIAL_EXEC;
		else if (strcmp(val, "local-exec") == 0)
			tls_model = TLSM_LOCAL_EXEC;
		else
			fatal("unknown TLS model '%s'", val);
		continue;
	}
	if (a[1] == 'f') continue;   /* -fxxx feature flags */
	if (a[1] == 'm') {           /* -march=, -mcpu=, -mfpu=, -mfloat-abi= */
		if (strncmp(a, "-march=", 7) == 0) {
			const char *march_val = a + 7;
			if (strncmp(march_val, "arm", 3) == 0) {
				/* ARM-specific march: pass through to ARM handler */
				arm_march = march_val;
			} else if (strcmp(march_val, "native") == 0) {
				/* -march=native: detect host CPU features.
				 * We can't call detect_cpu_features() here because
				 * the target arch isn't known yet; defer to after
				 * target selection. Store marker, process later. */
				arm_march = "armv7-a";  /* safe fallback for ARM */
				/* For x86_64/others, set native marker */
				static const char *march_native_marker = "native";
				(void)march_native_marker;
				extern int g_march_native_requested;
				g_march_native_requested = 1;
		} else if (strncmp(march_val, "x86-64-v", 8) == 0 ||
		           (march_val[0] == 'v' && march_val[1] >= '2')) {
			/* -march=x86-64-vN: map to feature bitmask */
			extern uint64_t march_x86_64_v_level(const char *);
			g_target_features |= march_x86_64_v_level(march_val);
		} else if (strncmp(march_val, "rv64", 4) == 0 ||
		           strncmp(march_val, "riscv64", 7) == 0) {
			/* RISC-V extension selection: parse rv64imafdc / rv64gc etc.
			 * The base ISA (rv64i) is implicit; each letter adds a feature
			 * bit. g_target_features is consumed by the riscv64 backend
			 * emit/isel for instruction gating. */
			const char *p = march_val;
			while (*p) {
				switch (*p) {
				case 'i': case 'I': break; /* base integer, always on */
				case 'm': case 'M': g_target_features |= 0; break; /* mul/div: baseline for rv64im */
				case 'a': case 'A': g_target_features |= 0; break; /* atomics: baseline */
				case 'f': case 'F': g_target_features |= MT_FEATURE_RV_F; break;
				case 'd': case 'D': g_target_features |= MT_FEATURE_RV_D; break;
				case 'c': case 'C': g_target_features |= MT_FEATURE_RV_C; break;
				case 'v': case 'V': g_target_features |= MT_FEATURE_RV_V; break;
				case 'g': /* g = imafd ("general") */
					g_target_features |= MT_FEATURE_RV_F | MT_FEATURE_RV_D;
					break;
				default: break; /* ignore unknown (e.g. trailing -linux) */
				}
				++p;
			}
		} else {
				/* Unknown march for non-ARM arch: store as-is in
				 * ARM variable to avoid silent ignore, but only
				 * apply if arch is ARM later. */
				arm_march = march_val;
			}
		}
		else if (strncmp(a, "-mcpu=", 6) == 0) arm_mcpu = a + 6;
		else if (strncmp(a, "-mfpu=", 6) == 0) arm_mfpu = a + 6;
		else if (strcmp(a, "-mfloat-abi=soft") == 0) arm_mfloat_abi = "soft";
		else if (strcmp(a, "-mfloat-abi=softfp") == 0) arm_mfloat_abi = "softfp";
		else if (strcmp(a, "-mfloat-abi=hard") == 0) arm_mfloat_abi = "hard";
		continue;
	}
	if (a[1] == 'M') {
		if (strcmp(a, "-M") == 0) {
			depmode = 1;
		} else if (strcmp(a, "-MM") == 0) {
			depmode = 2;
		} else if (strcmp(a, "-MD") == 0) {
			depmode = 3;
			depfile = ARGVAL("");
		} else if (strcmp(a, "-MMD") == 0) {
			depmode = 4;
			depfile = ARGVAL("");
		} else if (strncmp(a, "-MF", 3) == 0) {
			depfile = ARGVAL(a + 3);
		} else if (strncmp(a, "-MT", 3) == 0 ||
		           strncmp(a, "-MQ", 3) == 0) {
			(void)ARGVAL(a + 3);  /* dependency target name */
		} else if (strcmp(a, "-MP") == 0) {
			/* phony targets: recorded, no-op */
		} else {
			fprintf(stderr, "%s: unknown option '%s'\n", argv0, a);
			usage();
		}
		continue;
	}
	if (strcmp(a, "--target") == 0) { target = ARGVAL(""); continue; }
	if (strncmp(a, "--target=", 9) == 0) { target = a + 9; continue; }
	if (strcmp(a, "--sysroot") == 0) { sysroot = ARGVAL(""); continue; }
	if (strncmp(a, "--sysroot=", 10) == 0) { sysroot = a + 10; continue; }
	if (strcmp(a, "-target") == 0) { target = ARGVAL(""); continue; }
	if (strncmp(a, "-target=", 8) == 0) { target = a + 8; continue; }
	if (strncmp(a, "--specs=", 8) == 0) {
		if (strcmp(a + 8, "meuos") == 0)
			meuos_specs = true;
		else if (strcmp(a + 8, "host") == 0 || strcmp(a + 8, "system") == 0)
			meuos_specs_host = true;
		continue;
	}
	if (strcmp(a, "--specs") == 0) {
		char *spec = ARGVAL("");
		if (strcmp(spec, "meuos") == 0)
			meuos_specs = true;
		else if (strcmp(spec, "host") == 0 || strcmp(spec, "system") == 0)
			meuos_specs_host = true;
		continue;
	}
	/* p9-ui 诊断输出模式：--error-json 结构化错误，--explain 附加修复建议 */
	if (strcmp(a, "--error-json") == 0) {
		g_error_json = 1;
		continue;
	}
	if (strcmp(a, "--explain") == 0) {
		g_error_explain = 1;
		continue;
	}

		/* single-char options (with optional attached argument) */
		switch (a[1]) {
		case 'E': pponly = true; break;
		case 'S': emit_asm_only = true; break;
		case 'c': compile_only = true; break;
		case 'o': output = ARGVAL(a + 2); break;
		case 't': target = ARGVAL(a + 2); break;  /* alias for -target */
		case 'v': verbose = true; break;
		case 'w': warn_level = 0; break;
		case 'g': emit_debug = 1; break;   /* debug info */
		case 'd': { for (char *p = a + 2; *p; ++p) if (*p <= 'Z') debug[(unsigned char)*p] = 1; break; }
		case 'P': break;   /* suppress line markers in -E */
		case 'H': break;   /* print includes */
		case 'D': arrayaddptr(&defines, ARGVAL(a + 2)); break;
		case 'U': arrayaddptr(&undefs, ARGVAL(a + 2)); break;
		case 'I': arrayaddptr(&incdirs, ARGVAL(a + 2)); break;
		case 'L': arrayaddptr(&libdirs, ARGVAL(a + 2)); break;
		case 'l': arrayaddptr(&libs, ARGVAL(a + 2)); break;
		case 'O': {
			const char *lv = a[2] ? a + 2 : "1";
			if (lv[0] == 's' || lv[0] == 'f') {
				opt_level = (lv[0] == 's') ? 2 : 3;
				break;
			}
			if (isdigit((unsigned char)lv[0]))
				opt_level = lv[0] - '0';
			break;
		}
		default:
			fprintf(stderr, "%s: unknown option '%s'\n", argv0, a);
			usage();
		}
	}
#undef ARGVAL

	/* MeuOS is the default configuration when a sysroot is provided by the
	 * environment.  An explicit --specs=meuos also selects it.  Use --specs=host
	 * to force host-only mode (no MeuOS sysroot), overriding any MEUOS_SYSROOT. */
	if (!sysroot)
		sysroot = getenv("MEUOS_SYSROOT");
	/* If MEUOS_SYSROOT is set, MeuOS specs are the implicit default.
	 * Only --specs=host or --specs=system can override this. */
	if (sysroot && !meuos_specs_host && !meuos_specs)
		meuos_specs = true;
	if (meuos_specs && !sysroot)
		fprintf(stderr, "%s: --specs=meuos requires --sysroot or MEUOS_SYSROOT\n", argv0), exit(2);
	/* MeuOS specs select the project CRT and libc, not a mixture with the
	 * host C runtime.  The host compiler is still used only as assembler and
	 * linker during bootstrap. */
	if (meuos_specs) {
		nostdlib = true;
		/* MeuOS libc ships only static archives; the specs mode must produce
		 * fully static executables unless --shared or --pie was explicitly
		 * requested.  PIE outputs an ET_DYN with PT_INTERP while still
		 * linking static archives. */
		if (!shared && !pie)
			static_link = true;
	}
	/* For fully static non-PIC builds, all TLS is necessarily local —
	 * extern _Thread_local symbols must use LE (@ntpoff) instead of IE
	 * (@gotntpoff), because the static linker cannot resolve IE relocs
	 * without a GOT.  This overrides the TLSM_DEFAULT logic in valref(). */
	if (static_link && !pic && tls_model == TLSM_DEFAULT)
		tls_model = TLSM_LOCAL_EXEC;
	if (sysroot) {
		/* If sysroot is a .msys file, open via VFS instead of extracting */
		if (msys_is_sysroot(sysroot)) {
			msys_handle = msys_sysroot_open(sysroot);
			if (msys_handle) {
				msys_sysroot_path = sysroot;
				/* Auto-detect target architecture from @meuos_arch metadata
				 * when --target is not explicitly set. */
				if (!target) {
					char *arch = msys_sysroot_get_arch(msys_handle);
					if (arch) {
						/* Normalize: strip trailing newline if present */
						size_t alen = strlen(arch);
						while (alen > 0 && (arch[alen-1] == '\n' || arch[alen-1] == '\r'))
							arch[--alen] = '\0';
						if (alen > 0)
							target = arch;  /* arch is malloc'd; leaks intentionally */
					}
				}
			}
		}
		/* For .msys sysroots, skip libdirs (lib/ and usr/lib/ do not exist
		 * as real directories — library files are accessed via VFS). */
		if (!msys_is_sysroot(sysroot)) {
			arrayaddptr(&libdirs, sysrootpath(sysroot, "lib"));
			arrayaddptr(&libdirs, sysrootpath(sysroot, "usr/lib"));
		}
	}

	/* An invocation containing only linkable inputs must bypass lexing and
	 * parsing entirely.  Make-style builds use precisely this form for the
	 * final executable link. */
	if (inputs.len) {
		char **inp = inputs.val;
		bool link_only = true;
		for (i = 0; i < (int)(inputs.len / sizeof(char *)); ++i)
			if (!is_link_input(inp[i]))
				link_only = false;
		if (link_only) {
			if (pponly || emit_asm_only || compile_only)
				usage();
			run_host_link(&inputs, output ? output : "a.out", verbose,
				&libdirs, &libs, static_link, shared, pie, nostdlib,
				nodefaultlibs, meuos_specs, target);
			return 0;
		}
	}

	/* ----- IR backend + frontend init ----- */
	T = *pick_target(target);
	T.pic = pic;

	/* -march=native: now that we know the target architecture, detect
	 * host CPU features and merge into g_target_features. */
	if (g_march_native_requested) {
		const char *arch = targ_name(target);
		if (arch) {
			uint64_t host = detect_cpu_features(arch);
			g_target_features |= host;
		}
	}

	/* Wire ISA feature bitmask into the target structure so emit/isel
	 * stages can gate instructions on available CPU extensions. */
	T.features = g_target_features;

	targinit(targ_name(target));
	/* IR's global `typ[]` table is normally populated by the text-IL
	 * parser. mcc constructs IR directly, so no text IL is ever parsed -
	 * but IR ABI passes still dereference `typ` for aggregate-typed
	 * functions. Initialize to a valid zero-length Vec to avoid UB. */
	typ = vnew(0, sizeof typ[0], PHeap);

	tokeninit();
	/* Target predefined macros are part of the public C ABI contract.
	 * These mirror GCC's built-in target macros so libc/runtime code
	 * can branch on architecture via #ifdef __<arch>__.  The macro
	 * set matches what GCC defines for the same triplet, enabling
	 * shared source between mcc and host-built code. */
	if (target) {
		const char *name = targ_name(target);
		if (!name) {
			/* Fall through; host default is x86_64-sysv on Linux. */
		} else if (strncmp(name, "i386", 4) == 0) {
			ppdefine("__i386__", "1");
			ppdefine("__i386", "1");
			ppdefine("__ILP32__", "1");
			ppdefine("_ILP32", "1");
		} else if (strncmp(name, "x86_64", 6) == 0) {
			ppdefine("__x86_64__", "1");
			ppdefine("__x86_64", "1");
			ppdefine("__amd64__", "1");
			ppdefine("__amd64", "1");
			ppdefine("__LP64__", "1");
			ppdefine("_LP64", "1");
		} else if (strncmp(name, "aarch64", 7) == 0) {
			ppdefine("__aarch64__", "1");
			ppdefine("__aarch64", "1");
			ppdefine("__LP64__", "1");
			ppdefine("_LP64", "1");
		} else if (strncmp(name, "riscv64", 7) == 0) {
			ppdefine("__riscv", "1");
			ppdefine("__riscv_xlen", "64");
			ppdefine("__riscv64", "1");
			ppdefine("__LP64__", "1");
			ppdefine("_LP64", "1");
		} else if (strncmp(name, "loongarch64", 11) == 0) {
			ppdefine("__loongarch64", "1");
			ppdefine("__loongarch_lp64", "1");
			ppdefine("__LP64__", "1");
			ppdefine("_LP64", "1");
		} else if (strncmp(name, "arm", 3) == 0) {
			arm_set_target_defines();
		}
	}

	/* Apply command-line macro definitions / undefs / include paths
	 * before the first token is read. */
	for (i = 0; i < (int)(defines.len / sizeof(char *)); ++i) {
		char *d = ((char **)defines.val)[i];
		char *eq = strchr(d, '=');
		if (eq) {
			*eq = '\0';
			ppdefine(d, eq + 1);
			*eq = '=';
		} else {
			ppdefine(d, "1");
		}
	}
	for (i = 0; i < (int)(undefs.len / sizeof(char *)); ++i)
		ppundef(((char **)undefs.val)[i]);
	for (i = 0; i < (int)(incdirs.len / sizeof(char *)); ++i)
		ppincludepath(((char **)incdirs.val)[i]);
	if (sysroot && !nostdinc) {
		if (msys_sysroot_handle) {
			/* For .msys sysroots, register VFS include prefixes.
			 * pp.c's openinclude() handles these via msys_fopen
			 * directly, avoiding filesystem probe failure. */
			const char *pfx[8];
			int npfx = msys_sysroot_incprefixes(pfx, 8);
			for (int i = 0; i < npfx; i++) {
				char *vpath = xmalloc(strlen(msys_sysroot_path) + 1 + strlen(pfx[i]) + 1);
				sprintf(vpath, "%s/%s", msys_sysroot_path, pfx[i]);
				ppincludepath(vpath);
			}
		} else {
			ppincludepath(sysrootpath(sysroot, "include"));
			ppincludepath(sysrootpath(sysroot, "usr/include"));
		}
	}
	/* ----- input setup ----- */
	if (inputs.len) {
		char **inp = inputs.val;
		/* push in reverse so the first input is read first */
		for (i = (int)(inputs.len / sizeof(char *)) - 1; i >= 0; --i)
			scanfrom(inp[i], NULL);
		scanopen();
		first_input = inp[0];
	} else {
		scanfrom("<stdin>", stdin);
	}

	/* ----- -M / -MM : preprocess to discover includes, then emit deps ----- */
	if (depmode == 1 || depmode == 2) {
		char *tgt;
		ppinit();
		ppflags |= PPNEWLINE;
		/* run the preprocessor discarding token output; #include
		 * directives still execute and populate the dependency list */
		while (tok.kind != TEOF)
			next();
		tgt = output ? output : default_out_name(first_input, ".o");
		ppdumpdeps(stdout, tgt, first_input ? first_input : "<stdin>");
		return 0;
	}

	/* ----- output routing -----
	 *   - -E      -> preprocessed tokens to stdout (or -o file)
	 *   - -S      -> asm to stdout (or -o file)
	 *   - -c/default -> asm to a temp file, then host cc assembles (+links) */
	char asm_tmp_path[] = "/tmp/mccXXXXXX";
	int asm_fd = -1;
	FILE *asm_out = stdout;

	if (pponly) {
		if (output && !freopen(output, "w", stdout))
			fatal("open %s:", output);
	} else if (emit_asm_only) {
		if (output && !freopen(output, "w", stdout))
			fatal("open %s:", output);
	} else {
		asm_fd = mkstemp(asm_tmp_path);
		if (asm_fd < 0)
			fatal("mkstemp:");
		asm_out = fdopen(asm_fd, "w");
		if (!asm_out)
			fatal("fdopen:");
		/* Redirect frontend writes (printf etc.) and IR emitfn
		 * output to the asm temp file. */
		if (!freopen(asm_tmp_path, "w", stdout))
			fatal("freopen %s:", asm_tmp_path);
	}
	(void)asm_out;

	ppinit();
	if (pponly) {
		ppflags |= PPNEWLINE;
		while (tok.kind != TEOF) {
			tokenprint(&tok, stdout);
			next();
		}
	} else {
		scopeinit();
		if (emit_debug && first_input)
			emitdbgfile(first_input, stdout);
		while (tok.kind != TEOF) {
			if (!decl(&filescope, NULL)) {
				if (tok.kind == TSEMICOLON)
					error(&tok.loc, "unexpected ';' at top-level");
				error(&tok.loc, "expected declaration or function definition");
			}
		}
		emittentativedefns();

		/* Emit ELF footer (sections, etc.). */
		if (T.emitfin)
			T.emitfin(stdout);
	}

	fflush(stdout);
	if (ferror(stdout))
		fatal("write failed");

	if (!emit_asm_only && !pponly) {
		char *outpath = output;
		/* Hand off to host cc for final asm (+link). */
		if (compile_only) {
			if (!outpath)
				outpath = default_out_name(first_input, ".o");
			fclose(stdout);
			run_host_cc(asm_tmp_path, outpath, true, verbose,
			            &libdirs, &libs, static_link, shared, pie,
			            nostdlib, nodefaultlibs,
			            meuos_specs, target);
		} else {
			if (!outpath)
				outpath = "a.out";
			fclose(stdout);
			run_host_cc(asm_tmp_path, outpath, false, verbose,
			            &libdirs, &libs, static_link, shared, pie,
			            nostdlib, nodefaultlibs,
			            meuos_specs, target);
		}
		unlink(asm_tmp_path);

		/* -MD / -MMD : also write a dependency file alongside. */
		if (depmode == 3 || depmode == 4) {
			const char *tgt = output ? output :
				default_out_name(first_input, ".o");
			FILE *df = fopen(depfile, "w");
			if (!df)
				fatal("open %s:", depfile);
			ppdumpdeps(df, tgt, first_input ? first_input : "<stdin>");
			fclose(df);
		}
	}

	return 0;
}
