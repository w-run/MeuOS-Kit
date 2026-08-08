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
#include "i18n.h"

extern int emit_debug;  /* from emit/emit.c */
extern void emitdbgfile(char *, FILE *);  /* from emit/emit.c */

/* Forward declaration: detect CPU features by arch name, returns
 * MT_FEATURE_* bitmask. Defined in cpu_detect.c. */
extern uint64_t detect_cpu_features(const char *arch);

/* Global msys handle and path exposed to the preprocessor (pp.c) for
 * VFS-based include file reading (msys_fopen fallback). */
struct msys *msys_sysroot_handle;
const char *msys_sysroot_path;
/* The effective sysroot resolved by the driver: command-line --sysroot wins,
 * else MEUOS_SYSROOT env.  Consumed by host_toolchain.c (link-time decisions
 * such as whether to auto-link libgcc-meuos.a under --specs=meuos). */
const char *driver_sysroot;

/* Global IR backend state (declared extern in ir.h).
 * Per-arch Target objects are declared extern in driver_internal.h. */
Target T;
char debug['Z' + 1];
int opt_level = 2;    /* -O2 default */
/* -O 级别语义标志（详见 usage.c 的 -O<level> 文档）：
 *   -O0/-O1/-O2/-O3 用 opt_level 分级；
 *   -Og = -O1 级别 + g_force_fp（保留帧指针，调试友好）；
 *   -Os/-Oz = -O2 级别 + g_opt_size（尺寸导向；当前无尺寸优先指令
 *   选择，与 -O2 同管线，差距已记录）；-Ofast = -O3 + g_fast_math
 *   （当前无 fast-math 折叠语义，仅区分标志）。 */
/* g_force_fp（-Og）定义在 src/target/x86_64/x86_64_emit.c：check-mir-*
 * 单测链接 libmcc.a 中的后端对象，全局必须落在后端层（ir.h extern）。
 * g_opt_size（-Os/-Oz）定义在 src/mir/passes.c（同理由）。 */
int g_opt_z;      /* -Oz: aggressive size (same pipeline as -Os for now) */
/* g_fast_math（-Ofast 门控）定义在 src/mir/passes.c（mir_test 独立链接） */
/* warn_level and warn_as_error are defined in src/lex/token.c */
enum tls_model tls_model = TLSM_DEFAULT;

/* ARM sub-architecture flags (set via -march/-mcpu/-mfpu/-mfloat-abi) */
static const char *arm_march  = "armv7-a";
static const char *arm_mcpu   = NULL;
static const char *arm_mfpu   = "vfpv3-d16";
static const char *arm_mfloat_abi = "hard";

/* Language selection: 0 = C (mcc default), 1 = C++ (m++ default).
 * Set by mpp_main before calling mcc_main, or via MCC_LANG for testing;
 * the input loop may also switch to C++ on .cc/.cpp suffixes. */
int g_lang;

/* Set by typequal() while a C++20 `consteval` function-specifier is being
 * parsed (the C lexer sees it as an identifier); decl.c consumes it to mark
 * the decl isconsteval so call sites can enforce immediate evaluation. */
int g_cpp_func_consteval;

/* Non-zero while a `consteval` function body is being parsed: the body is
 * a constant context, so call sites inside it suspend the
 * immediate-invocation check (recursion/helper calls are folded when the
 * enclosing call is evaluated). */
int g_cpp_in_consteval_body;

/* -std=<standard> language mode (semantic): 0 = unspecified (default,
 * historical behavior), then c89/c99/c11/c17/c23 then c++98..c++23.
 * Consumed by ppinit() to define __STDC_VERSION__/__cplusplus. */
int g_std_mode;

/* Global target features bitmask (MT_FEATURE_*), set by -march=native or
 * -march=x86-64-vN. 0 = baseline only. Used by backend emit to gate ISA
 * levels (second phase); for now it records the user's intent. */
uint64_t g_target_features = 0;
int g_arm_arch_ver = 7;  /* ARM architecture version (default armv7) */
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
	g_arm_arch_ver = arch_ver;
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

/* mcc_main is the shared compiler driver, invoked by both the mcc binary
 * (default language: C) and the m++ binary (default language: C++ via the
 * C++ frontend under construction).  Renamed from main() so the m++ entry
 * point (mpp_main.c) can wrap it without symbol conflicts. */
int
mcc_main(int argc, char *argv[])
{
	struct array inputs = {0}, incdirs = {0}, libdirs = {0}, libs = {0};
	struct array defines = {0}, undefs = {0};
	struct array wa_args = {0}, wl_args = {0};  /* -Wa,/-Wl, passthrough */
	bool pponly = false, emit_asm_only = false, compile_only = false;
	bool verbose = false, nostdinc = false, nostdlib = false, nodefaultlibs = false;
	bool static_link = false, shared = false, pic = false, pie = false;
	bool meuos_specs = false, meuos_specs_host = false;
	char *output = NULL, *target = NULL, *first_input = NULL, *sysroot = NULL;
	struct msys *msys_handle = NULL;
	int depmode = 0;       /* 0 none, 1 -M, 2 -MM, 3 -MD, 4 -MMD */
	char *depfile = NULL;
	int i;

	/* MIR-only (Phase 2): MIR is the sole frontend-to-backend lowering
	 * path.  g_use_mir is forced to 1; the legacy direct-LIR path
	 * (MCC_USE_MIR=0) no longer exists. */
	extern int g_use_mir;
	g_use_mir = 1;

	/* P2+ MIR-native backend (Phase 2, Phase 4 step 2): always enabled.
	 * Each target's machine backend runs first; if it rejects a construct
	 * (e.g. arm/i386 aggregates), the LIR bridge fallback takes over.
	 * The MCC_MIR_BACKEND env var was removed in Phase 2 — the MIR
	 * backend is no longer optional. */
	extern int g_use_mir_backend;
	g_use_mir_backend = 1;

	/* Language: 0 = C (default for mcc), 1 = C++ (default for m++).
	 * The m++ driver sets g_lang=1 before calling mcc_main; file suffix
	 * detection (.cc/.cpp) also switches to C++ in the input loop. */
	extern int g_lang;
	if (getenv("MCC_LANG"))
		g_lang = atoi(getenv("MCC_LANG"));

	/* Normalize argv: gcc/clang compatibility allows single-dash
	 * multi-letter options (-static, -shared, ...) which we rewrite
	 * to the standard double-dash form (--static, --shared, ...).
	 * Single-letter short options and category-prefixed options
	 * (-W<w>, -f<f>, -m<m>) are left untouched. See arg_compat.c. */
	argv = arg_normalize(argc, argv);
	argv0 = progname(argv[0], "mcc");

	/* i18n 默认语言：LANG/LC_MESSAGES 以 zh* 开头则推断中文，否则英文。
	 * --lang=en|zh 显式参数在后面解析时覆盖此推断。 */
	{
		const char *lc = getenv("LC_MESSAGES");
		if (!lc || !*lc)
			lc = getenv("LANG");
		if (lc && strncmp(lc, "zh", 2) == 0)
			g_msg_lang = 1;
	}

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
		/* --verbose: alias for -v (print each executed driver command) */
		if (strcmp(a, "--verbose") == 0) { verbose = true; continue; }
		/* --color[=auto|always|never]: diagnostic color control.
		 * Default (auto) colors stderr only when it is a tty. */
		if (strcmp(a, "--color") == 0) {
			extern int g_diag_color;
			g_diag_color = 1;
			continue;
		}
		if (strncmp(a, "--color=", 8) == 0) {
			extern int g_diag_color;
			const char *v = a + 8;
			if (strcmp(v, "auto") == 0)
				g_diag_color = -1;
			else if (strcmp(v, "always") == 0)
				g_diag_color = 1;
			else if (strcmp(v, "never") == 0)
				g_diag_color = 0;
			else
				fatal("unknown --color mode '%s' (use auto/always/never)", v);
			continue;
		}
		/* -x <lang>: force the input language, overriding the driver's
		 * default (mcc=C, m++=C++). */
		if (strcmp(a, "-x") == 0 || strncmp(a, "-x", 2) == 0 && a[2]) {
			char *lang = ARGVAL(a + 2);
			if (strcmp(lang, "c") == 0 || strcmp(lang, "c-header") == 0 ||
			    strcmp(lang, "cpp-output") == 0)
				g_lang = 0;
			else if (strcmp(lang, "c++") == 0 ||
			    strcmp(lang, "c++-header") == 0 ||
			    strcmp(lang, "c++-cpp-output") == 0)
				g_lang = 1;
			else
				fatal("unknown language '%s' for -x (use c or c++)", lang);
			continue;
		}
		/* -Wa,<args> / -Wl,<args>: pass assembler/linker options through
		 * to the host toolchain (accepted; forwarded verbatim). */
		if (strncmp(a, "-Wa,", 4) == 0) {
			arrayaddptr(&wa_args, a + 4);
			continue;
		}
		if (strncmp(a, "-Wl,", 4) == 0) {
			arrayaddptr(&wl_args, a + 4);
			continue;
		}

		/* whole-arg multi-letter options (double-dash standard form;
		 * single-dash gcc forms are normalized by arg_normalize()).
		 * Category-prefixed options (-W<w>, -f<f>, -m<m>, -M<d>) stay
		 * single-dash and are handled below. */
	if (strncmp(a, "--std=", 6) == 0) {
		/* -std=<standard>: select the language mode.  Maps to the
		 * standard version macros via g_std_mode (ppinit). */
		const char *s = a + 6;
		if (strcmp(s, "c89") == 0 || strcmp(s, "c90") == 0 ||
		    strcmp(s, "gnu89") == 0 || strcmp(s, "iso9899:1990") == 0)
			g_std_mode = 1;
		else if (strcmp(s, "c99") == 0 || strcmp(s, "c9x") == 0 ||
		    strcmp(s, "gnu99") == 0 || strcmp(s, "iso9899:1999") == 0)
			g_std_mode = 2;
		else if (strcmp(s, "c11") == 0 || strcmp(s, "c1x") == 0 ||
		    strcmp(s, "gnu11") == 0 || strcmp(s, "iso9899:2011") == 0)
			g_std_mode = 3;
		else if (strcmp(s, "c17") == 0 || strcmp(s, "c18") == 0 ||
		    strcmp(s, "gnu17") == 0 || strcmp(s, "iso9899:2017") == 0)
			g_std_mode = 4;
		else if (strcmp(s, "c23") == 0 || strcmp(s, "c2x") == 0 ||
		    strcmp(s, "gnu23") == 0 || strcmp(s, "iso9899:2024") == 0)
			g_std_mode = 5;
		else if (strcmp(s, "c++98") == 0 || strcmp(s, "c++03") == 0)
			g_std_mode = 6;
		else if (strcmp(s, "c++11") == 0 || strcmp(s, "c++0x") == 0)
			g_std_mode = 7;
		else if (strcmp(s, "c++14") == 0 || strcmp(s, "c++1y") == 0)
			g_std_mode = 8;
		else if (strcmp(s, "c++17") == 0 || strcmp(s, "c++1z") == 0)
			g_std_mode = 9;
		else if (strcmp(s, "c++20") == 0 || strcmp(s, "c++2a") == 0)
			g_std_mode = 10;
		else if (strcmp(s, "c++23") == 0 || strcmp(s, "c++2b") == 0)
			g_std_mode = 11;
		else
			fatal("unknown -std= value '%s'", s);
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
	/* -Wall/-Wextra/-Werror: -W is a category prefix, stays single-dash.
	 * -Wall: gcc 语义常用组（含 -Wunused-variable/-Wuninitialized）；
	 * -Wextra: -Wall + 额外（含 -Wunused-parameter/-Wsign-compare）。 */
	if (strcmp(a, "-Wall") == 0) {
		warn_level = WARN_WALL;
		continue;
	}
	if (strcmp(a, "-Wextra") == 0) {
		warn_level = WARN_WEXTRA;
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
	if (a[1] == 'W') {
		/* fine-grained -W control: -Wno-error / -Wno-all disable the
		 * corresponding groups; -Wunused-variable/-Wunused-parameter/
		 * -Wconversion/-Wsign-compare/-Wuninitialized (及 -Wno- 反形式)
		 * 独立开关细粒度警告；其他 -Wxxx 标志按 gcc 兼容性接受。 */
		if (strcmp(a, "-Wno-error") == 0) {
			warn_as_error = false;
			warn_level &= ~WARN_ERROR;
		} else if (strcmp(a, "-Wno-all") == 0) {
			warn_level &= ~WARN_WALL;
		} else if (strcmp(a, "-Wunused-variable") == 0) {
			warn_level |= WARN_UNUSED_VAR;
		} else if (strcmp(a, "-Wno-unused-variable") == 0) {
			warn_level &= ~WARN_UNUSED_VAR;
		} else if (strcmp(a, "-Wunused-parameter") == 0) {
			warn_level |= WARN_UNUSED_PARAM;
		} else if (strcmp(a, "-Wno-unused-parameter") == 0) {
			warn_level &= ~WARN_UNUSED_PARAM;
		} else if (strcmp(a, "-Wconversion") == 0) {
			warn_level |= WARN_CONVERSION;
		} else if (strcmp(a, "-Wno-conversion") == 0) {
			warn_level &= ~WARN_CONVERSION;
		} else if (strcmp(a, "-Wsign-compare") == 0) {
			warn_level |= WARN_SIGN_COMPARE;
		} else if (strcmp(a, "-Wno-sign-compare") == 0) {
			warn_level &= ~WARN_SIGN_COMPARE;
		} else if (strcmp(a, "-Wuninitialized") == 0) {
			warn_level |= WARN_UNINIT;
		} else if (strcmp(a, "-Wno-uninitialized") == 0) {
			warn_level &= ~WARN_UNINIT;
		}
		continue;   /* other -Wxxx warning flags */
	}
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
	if (a[1] == 'f') {
		/* -f/-fno- feature flags: the ones with a backend switch are
		 * wired; everything else is accepted (no-op) for gcc/clang
		 * compatibility instead of being silently dropped. */
		if (strcmp(a, "-fomit-frame-pointer") == 0)
			g_force_fp = 0;   /* permit omitting the frame pointer */
		else if (strcmp(a, "-fno-omit-frame-pointer") == 0)
			g_force_fp = 1;   /* force keeping the frame pointer */
		else if (strcmp(a, "-fno-strict-aliasing") == 0)
			; /* accepted: no TBAA pass to disable */
		continue;
	}
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
		} else if (strncmp(march_val, "i48", 3) == 0 || strncmp(march_val, "i58", 3) == 0 || strncmp(march_val, "i68", 3) == 0) {
			g_target_features |= MT_FEATURE_I386_CMPXCHG;
			if (strncmp(march_val, "i58", 3) == 0)
				g_target_features |= MT_FEATURE_I386_FPU;
			if (strncmp(march_val, "i68", 3) == 0)
				g_target_features |= MT_FEATURE_I386_CMPXCHG8B;
			/* i386 march — arm_march intentionally not set */
		} else if (strncmp(march_val, "armv8.", 6) == 0) {
			arm_march = march_val;
			if (strstr(march_val, "8.2") || strstr(march_val, "8.3") ||
			    strstr(march_val, "8.4") || strstr(march_val, "8.5")) {
				g_target_features |= MT_FEATURE_AARCH64_FP16;
				g_target_features |= MT_FEATURE_AARCH64_RDM;
			}
			if (strstr(march_val, "8.3") || strstr(march_val, "8.4") ||
			    strstr(march_val, "8.5"))
				g_target_features |= MT_FEATURE_AARCH64_JSCVT;
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
		} else if (strcmp(a, "-MMD") == 0) {
			depmode = 4;
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
	if (strcmp(a, "--meuos") == 0) {
		meuos_specs = true;
		continue;
	}
	/* p9-ui 诊断输出模式：--error-json 结构化错误，--explain 附加修复建议 */
	if (strcmp(a, "--error-json") == 0) {
		g_error_json = 1;
		g_diag_fmt = DIAG_JSON;
		continue;
	}
	if (strcmp(a, "--explain") == 0) {
		g_error_explain = 1;
		continue;
	}
	/* --error-format=<text|json|sarif>: uniform format switch for errors and
	 * warnings.  text (default) keeps the current colored text + caret; json
	 * is the same as --error-json; sarif is declared but not yet mapped. */
	if (strncmp(a, "--error-format=", 15) == 0) {
		const char *fmt = a + 15;
		if (strcmp(fmt, "text") == 0) {
			g_diag_fmt = DIAG_TEXT;
			g_error_json = 0;
		} else if (strcmp(fmt, "json") == 0) {
			g_diag_fmt = DIAG_JSON;
			g_error_json = 1;
		} else if (strcmp(fmt, "sarif") == 0) {
			g_diag_fmt = DIAG_SARIF;
			g_error_json = 0; /* sarif uses its own envelope, not line-JSON */
		} else {
			fatal("unknown --error-format '%s' (use text/json/sarif)", fmt);
		}
		continue;
	}
	/* i18n：--lang=en|zh 选择诊断/帮助语言（覆盖 LANG 环境推断）。 */
	if (strncmp(a, "--lang=", 7) == 0) {
		const char *lg = a + 7;
		if (strncmp(lg, "zh", 2) == 0)
			g_msg_lang = 1;
		else if (strncmp(lg, "en", 2) == 0)
			g_msg_lang = 0;
		else
			fatal("unknown language '%s' (use en or zh)", lg);
		continue;
	}

		/* single-char options (with optional attached argument) */
		switch (a[1]) {
		case 'E': pponly = true; break;
		case 'S': emit_asm_only = true; break;
		case 'c': compile_only = true; break;
		case 'p': if (strcmp(a, "-pg") == 0) break; /* gprof profiling: accepted (no-op) */
		case 'o': output = ARGVAL(a + 2); break;
		case 't': target = ARGVAL(a + 2); break;  /* alias for -target */
		case 'v': verbose = true; break;
		case 'w': warn_level = 0; break;
		case 'g': {
			/* -g (default level 1) / -g0 (no debug info at all) /
			 * -gN (N = 1,2,4,5) / -gdwarf[-N]: record the DWARF level.
			 * -g0 turns off emit_debug so no .file/.loc/.debug_* output
			 * is produced; higher levels are accepted and recorded but
			 * currently share the same minimal DWARF emission. */
			const char *lv = a[2] ? a + 2 : "";
			int lvl = 1;
			if (lv[0] == '0' && lv[1] == '\0')
				lvl = 0;
			else if (strncmp(lv, "dwarf", 5) == 0)
				lvl = lv[5] ? atoi(lv + 5) : 2;
			else if (lv[0] >= '1' && lv[0] <= '9')
				lvl = atoi(lv);
			if (lvl < 0) lvl = 0;
			if (lvl > 5) lvl = 5;
			g_dwarf_level = lvl;
			emit_debug = lvl > 0;
			break;
		}
		case 'd': { for (char *p = a + 2; *p; ++p) {
			if (*p <= 'Z') debug[(unsigned char)*p] = 1;
			if (*p == 'P') g_opt_log = 1;   /* -dP: per-pass optimizer log */
			if (*p == 'M') g_opt_snapshot = 1; /* -dM: per-pass MIR snapshot */
		} break; }
		case 'P': break;   /* suppress line markers in -E */
		case 'H': break;   /* print includes */
		case 'D': arrayaddptr(&defines, ARGVAL(a + 2)); break;
		case 'U': arrayaddptr(&undefs, ARGVAL(a + 2)); break;
		case 'I': arrayaddptr(&incdirs, ARGVAL(a + 2)); break;
		case 'L': arrayaddptr(&libdirs, ARGVAL(a + 2)); break;
		case 'l': arrayaddptr(&libs, ARGVAL(a + 2)); break;
		case 'O': {
			const char *lv = a[2] ? a + 2 : "1";
			/* -Og: debug-friendly — O1-level passes, keep frame pointer */
			if (lv[0] == 'g') {
				opt_level = 1;
				g_force_fp = 1;
				break;
			}
			/* -Os: size-oriented (O2 level); -Oz: aggressive size (same
			 * pipeline as -Os for now, see g_opt_z note above) */
			if (lv[0] == 's') {
				opt_level = 2;
				g_opt_size = 1;
				break;
			}
			if (lv[0] == 'z') {
				opt_level = 2;
				g_opt_size = 1;
				g_opt_z = 1;
				break;
			}
			/* -Ofast: O3 level + fast-math semantics (flag only for now) */
			if (lv[0] == 'f') {
				opt_level = 3;
				g_fast_math = 1;
				break;
			}
			if (isdigit((unsigned char)lv[0])) {
				int n = lv[0] - '0';
				if (n > 3) {
					fprintf(stderr, "%s: warning: invalid optimization "
					        "level '-O%d' (clamping to -O3)\n", argv0, n);
					n = 3;
				}
				opt_level = n;
				break;
			}
			fprintf(stderr, "%s: unknown optimization level '%s'\n",
			        argv0, a);
			usage();
		}
		default:
			fprintf(stderr, "%s: unknown option '%s'\n", argv0, a);
			usage();
		}
	}
#undef ARGVAL

	/* MeuOS specs must be requested explicitly (--specs=meuos / --meuos).
	 * The old implicit default when MEUOS_SYSROOT was set polluted ordinary
	 * compilation and has been removed.  Use --specs=host for host-only mode. */
	if (!sysroot)
		sysroot = getenv("MEUOS_SYSROOT");
	/* --specs=meuos must be explicit.  The old implicit default (activate
	 * MeuOS specs whenever MEUOS_SYSROOT was set) polluted ordinary
	 * compilation.  User must opt in via --specs=meuos / --meuos. */
	if (meuos_specs && !sysroot)
		fprintf(stderr, "%s: --specs=meuos requires --sysroot or MEUOS_SYSROOT\n", argv0), exit(2);
	driver_sysroot = sysroot; /* record effective sysroot for host toolchain */
	/* MeuOS specs select the project CRT and libc, not a mixture with the
	 * host C runtime.  The host compiler is still used only as assembler and
	 * linker during bootstrap. */
	if (meuos_specs) {
		extern int g_meuos_specs;
		g_meuos_specs = 1;
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
				/* publish to the global handle so pp.c's openinclude()
				 * can serve <...> headers via msys_fopen() */
				msys_sysroot_handle = msys_handle;
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
				nodefaultlibs, meuos_specs, target, &wl_args);
			return 0;
		}
	}

	/* ----- IR backend + frontend init ----- */
	T = *pick_target(target);
	T.pic = pic;
	/* PIC/shared mirror for the MIR machine layer (x86_64_memit.c),
	 * which does not read the QBE `Target T` global (purity rule). */
	extern int g_pic;
	g_pic = pic;
	/* TLS access-model mirror for the MIR machine layer.  x86_64_memit.c
	 * consults g_tls_model to pick general-dynamic (GD) vs initial-exec
	 * (IE) vs local-exec (LE) emission; same purity rule as g_pic. */
	extern int g_tls_model;
	g_tls_model = tls_model;

	/* Triple → ABI auto-mapping (triple-abi-map). If the target triple
	 * contains an ABI suffix (e.g. riscv64-meuos-linux-lp64d), extract
	 * it and set the corresponding backend flags automatically without
	 * requiring -mabi=. */
	{	const char *abi = targ_abi(target);
		if (abi && abi[0]) {
			if (strcmp(abi, "lp64d") == 0 || strcmp(abi, "lp64f") == 0)
				arm_mfloat_abi = "hard";  /* riscv floating-point ABI */
			else if (strcmp(abi, "ilp32") == 0 || strcmp(abi, "lp64") == 0)
				arm_mfloat_abi = "soft";
			else if (strcmp(abi, "gnueabihf") == 0)
				arm_mfloat_abi = "hard";  /* ARM hard-float */
			else if (strcmp(abi, "gnu") == 0)
				arm_mfloat_abi = "soft";
		}
	}

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
		if (!asm_out) {
			close(asm_fd);
			fatal("fdopen:");
		}
		/* asm_out never writes: the frontend and IR emitfn output is
		 * redirected through the freopen below.  Close it (and thus the
		 * mkstemp descriptor) so the temp fd does not leak. */
		fclose(asm_out);
		asm_out = NULL;
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
		if (emit_debug && first_input) {
			emitdbgfile(first_input, stdout);
			dwarf_set_file(first_input);
		}
		if (g_lang == 1) {
			/* C++ translation unit: the C++ frontend parser drives the
			 * declaration loop (C++ grammar layered over the C parser). */
			extern void cpp_parse_translation_unit(void);
			cpp_parse_translation_unit();
		} else {
			/* Multi-error collection (--error-json): the loop arms a
			 * recovery jump buffer so error() can longjmp back after
			 * emitting each collected error and parsing resumes at the
			 * next top-level item (err_sync skips to the next ';'/'}').
			 * Without --error-json, error() exits on the first error. */
			while (tok.kind != TEOF) {
				if (g_error_json || g_diag_fmt == DIAG_SARIF) {
					if (setjmp(g_err_recovery) != 0) {
						g_err_recovery_set = 0;
						err_sync();
						continue;
					}
					g_err_recovery_set = 1;
				}
				if (!decl(&filescope, NULL)) {
					if (tok.kind == TSEMICOLON)
						error_code(E_SYNTAX, &tok.loc,
						    "unexpected ';' at top-level");
					error_code(E_SYNTAX, &tok.loc,
					    "expected declaration or function definition");
				}
				g_err_recovery_set = 0;
			}
		}
		if ((g_error_json || g_diag_fmt == DIAG_SARIF) && g_error_seen) {
			exit(1); /* errors were collected: fail even if under the limit */
		}
		emittentativedefns();

		/* Emit ELF footer (sections, etc.). */
		if (T.emitfin)
			T.emitfin(stdout);
		/* DWARF debug info (when -g with a level). */
		if (g_dwarf_level > 0)
			dwarf_finalize(stdout);
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
			            meuos_specs, target, &wa_args, &wl_args);
		} else {
			if (!outpath)
				outpath = "a.out";
			fclose(stdout);
			run_host_cc(asm_tmp_path, outpath, false, verbose,
			            &libdirs, &libs, static_link, shared, pie,
			            nostdlib, nodefaultlibs,
			            meuos_specs, target, &wa_args, &wl_args);
		}
		unlink(asm_tmp_path);

		/* -MD / -MMD : also write a dependency file alongside. */
		if (depmode == 3 || depmode == 4) {
			const char *tgt = output ? output :
				default_out_name(first_input, ".o");
			if (!depfile)
				depfile = default_out_name(
					output ? output : first_input, ".d");
			FILE *df = fopen(depfile, "w");
			if (!df)
				fatal("open %s:", depfile);
			ppdumpdeps(df, tgt, first_input ? first_input : "<stdin>");
			fclose(df);
		}
	}

	return 0;
}
