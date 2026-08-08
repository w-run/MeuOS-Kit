/* host_toolchain.c - hand emitted assembly / objects to the host
 * assembler and linker. This is a Phase-1 bootstrap convenience; later
 * phases replace the host `cc` with a self-hosted path.
 *
 * P3 integration: when the MT_AS / MT_LD environment variables are set
 * and the target is x86_64 (the only architecture mt/as currently
 * supports), the host `cc` is bypassed entirely and mcc drives mt/as
 * and mt/ld directly. This removes the last host-tool dependency for
 * x86_64 builds. Non-x86_64 targets and -shared fall back to the
 * original cc handoff.
 *
 * Cross-file entries: sysrootpath(), run_host_cc(), run_host_link(),
 * is_link_input(), default_out_name(). File-local: cmdadd(),
 * asm_requires_atomic(), mt_mode_enabled(), mt_target_supported(),
 * run_mt_as(), run_mt_ld(). */
#define _POSIX_C_SOURCE 200809L  /* mkstemp, getpid for mt handoff */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include "driver_internal.h"
#include "mt/target.h"

/* Global target feature bitmask set by main.c from -march= parsing. */
extern uint64_t g_target_features;

/* Forward declarations needed by mt_init() and resolve_arch_sysroot().
 * Both are defined later in this file. */
static const char *mt_target_name(const char *target_triple);

/* Derive a -march= value for mt/as from the compiler's resolved feature
 * bitmask, so the assembler enforces the same ISA level that mcc selected
 * (as-isa-gating integration).  Only x86_64 has ISA levels today. */
static const char *
mt_march_for_features(const char *mt_target, uint64_t features)
{
	if (strcmp(mt_target, "x86_64") != 0 || features == 0)
		return NULL;
	if (features & MT_FEATURE_AVX512F)
		return "x86-64-v4";
	if (features & MT_FEATURE_AVX2)
		return "x86-64-v3";
	if (features & (MT_FEATURE_SSE4_2 | MT_FEATURE_POPCNT))
		return "x86-64-v2";
	return NULL;
}

static void
cmdadd(struct array *cmd, const char *s)
{
	arrayaddbuf(cmd, s, strlen(s));
}

/* True if the target triplet selects a 32-bit x86 target.  i386 family
 * reuses the host x86_64 toolchain via -m32 (no separate cross prefix). */
static bool
target_is_i386(const char *target_triple)
{
	return target_triple && (
	    strncmp(target_triple, "i386", 4) == 0 ||
	    strncmp(target_triple, "i486", 4) == 0 ||
	    strncmp(target_triple, "i586", 4) == 0 ||
	    strncmp(target_triple, "i686", 4) == 0);
}

/* Pick the host cc that can assemble+link the emitted asm for the given
 * target.  For non-host 64-bit targets (aarch64/riscv64/loongarch64) we
 * delegate to the matching <arch>-linux-gnu-gcc cross toolchain so that
 * `mcc --target=aarch64` works out of the box.  MCC_HOST_CC / HOST_CC
 * override the auto-detection for self-host builds and unusual setups.
 *
 * The cross gcc only assembles/links; CRT objects and libc/libatomic
 * archives still come from the MeuOS sysroot via -L/-l flags.  This is a
 * Phase-1 bootstrap convenience - P10 (meuos-toolchain) removes the
 * dependency entirely. */
static const char *
pick_host_cc(const char *target_triple)
{
	const char *cc = getenv("MCC_HOST_CC");
	if (!cc)
		cc = getenv("HOST_CC");
	if (cc)
		return cc;
	if (!target_triple)
		return "cc";
	if (strncmp(target_triple, "aarch64", 7) == 0 ||
	    strncmp(target_triple, "arm64", 5) == 0)
		return "aarch64-linux-gnu-gcc";
	if (strncmp(target_triple, "riscv64", 7) == 0 ||
	    strncmp(target_triple, "rv64", 4) == 0)
		return "riscv64-linux-gnu-gcc";
	if (strncmp(target_triple, "loongarch64", 11) == 0 ||
	    strncmp(target_triple, "la64", 4) == 0)
		return "loongarch64-linux-gnu-gcc";
	/* i386 family uses `cc -m32` (handled by callers); other/unknown
	 * triplets default to the host cc. */
	return "cc";
}

char *
sysrootpath(const char *root, const char *suffix)
{
	size_t n = strlen(root);
	char *path;

	while (n && root[n - 1] == '/')
		--n;
	path = xmalloc(n + 1 + strlen(suffix) + 1);
	memcpy(path, root, n);
	path[n] = '/';
	strcpy(path + n + 1, suffix);
	return path;
}

/* The driver records the effective sysroot (command-line --sysroot wins,
 * else MEUOS_SYSROOT env); consume it here so link-time decisions align with
 * the sysroot that actually feeds -L/--sysroot to the linker. */
extern const char *driver_sysroot;

/* Resolve the effective sysroot for a given target triple.
 *
 * The MeuOS Kit sysroot layout supports two forms:
 *   1. Top-level: $MEUOS_SYSROOT/usr/lib/  — host arch (default for native)
 *   2. Arch-specific: $MEUOS_SYSROOT/<arch>/usr/lib/ — cross-compilation
 *
 * When (1) is used and the target arch's CRT lives in an arch-specific
 * subdirectory, this function auto-resolves so the linker finds matching
 * CRT and libc objects without requiring the caller to update MEUOS_SYSROOT.
 * Returns a pointer to a static buffer (valid until next call) or the
 * original pointer when no resolution is needed. */
const char *
resolve_arch_sysroot(const char *sysroot, const char *target_triple)
{
	static char buf[1024];
	const char *arch_name;
	char probe[1024];

	if (!sysroot || !*sysroot || !target_triple || !*target_triple)
		return sysroot;

	arch_name = mt_target_name(target_triple);
	if (!arch_name)
		return sysroot;

	/* Check if $sysroot/<arch>/usr/lib/crt1.o exists — that signals
	 * a top-level Kit sysroot with per-arch subdirectories. */
	snprintf(probe, sizeof(probe), "%s/%s/usr/lib/crt1.o", sysroot, arch_name);
	if (access(probe, R_OK) == 0) {
		snprintf(buf, sizeof(buf), "%s/%s", sysroot, arch_name);
		return buf;
	}

	/* No arch-specific CRT found; return the path as-is. */
	return sysroot;
}

/* Whether the active MeuOS sysroot provisions libgcc-meuos.a (libgcc-ABI soft
 * helpers: __divdi3/__udivdi3/__ctzdi2/...).  Old sysroots that predate the
 * archive skip the link flag so --specs=meuos still works there. */
static bool
sysroot_has_libgcc(void)
{
	const char *r = driver_sysroot ? driver_sysroot : getenv("MEUOS_SYSROOT");
	char p[1024];
	if (!r || !*r)
		return false;
	snprintf(p, sizeof p, "%s/usr/lib/libgcc-meuos.a", r);
	return access(p, R_OK) == 0;
}

/* Atomic RMW lowering uses the width-specific compiler-runtime ABI.  Inspect
 * the generated assembly immediately before the host link, so ordinary C
 * programs neither require nor accidentally acquire a libatomic dependency. */
static bool
asm_requires_atomic(const char *asm_path)
{
	FILE *f;
	char line[512];

	f = fopen(asm_path, "r");
	if (!f)
		fatal("open %s:", asm_path);
	while (fgets(line, sizeof(line), f)) {
		if (strstr(line, "__atomic_fetch_add_") ||
		    strstr(line, "__atomic_fetch_sub_") ||
		    strstr(line, "__atomic_fetch_and_") ||
		    strstr(line, "__atomic_fetch_or_") ||
		    strstr(line, "__atomic_fetch_xor_") ||
		    strstr(line, "__atomic_exchange_") ||
		    strstr(line, "__atomic_compare_exchange_") ||
		    strstr(line, "atomic_thread_fence") ||
		    strstr(line, "atomic_signal_fence") ||
		    strstr(line, "__atomic_load_") ||
		    strstr(line, "__atomic_store_")) {
			fclose(f);
			return true;
		}
	}
	fclose(f);
	return false;
}

/* Static buffers for auto-discovered MeuOS toolchain (mt/as, mt/ld)
 * paths.  Populated by mt_init() from mcc's own binary location.
 * Env vars MT_AS/MT_LD take precedence when both are set. */
static char mt_as_auto[1024];
static char mt_ld_auto[1024];

/* Auto-discover mt/as and mt/ld relative to the mcc binary's own
 * directory.  Called once from main.c after argv parsing.
 * Env vars MT_AS/MT_LD are authoritative when both are set. */
void
mt_init(const char *mcc_binary_path)
{
	char real[4096];
	char *slash;

	if (getenv("MT_AS") && getenv("MT_LD"))
		return;
	if (!mcc_binary_path || !*mcc_binary_path)
		return;

	if (!realpath(mcc_binary_path, real))
		return;
	slash = strrchr(real, '/');
	if (!slash)
		return;
	*slash = '\0';  /* real = directory containing mcc */

	/* Kit layout: mcc in projects/mcc/, toolchain in
	 * projects/meuos-toolchain/build/bin/{as,ld}. */
	snprintf(mt_as_auto, sizeof(mt_as_auto),
	         "%s/../meuos-toolchain/build/bin/as", real);
	snprintf(mt_ld_auto, sizeof(mt_ld_auto),
	         "%s/../meuos-toolchain/build/bin/ld", real);

	if (access(mt_as_auto, X_OK) != 0 || access(mt_ld_auto, X_OK) != 0) {
		mt_as_auto[0] = '\0';
		mt_ld_auto[0] = '\0';
	}
}

/* P3: mt integration.  MT_AS and MT_LD (env var or auto-discovered)
 * together select the MeuOS toolchain handoff.  Either one alone is
 * treated as not-configured so partially-set environments fall back
 * to the host cc path. */
static bool
mt_mode_enabled(void)
{
	if (getenv("MT_AS") != NULL && getenv("MT_LD") != NULL)
		return true;
	return mt_as_auto[0] != '\0' && mt_ld_auto[0] != '\0';
}

/* Return the resolved mt/as path: env var wins, else auto-discovered. */
static const char *
mt_as_resolved(void)
{
	const char *e = getenv("MT_AS");
	return e ? e : mt_as_auto;
}

/* Return the resolved mt/ld path: env var wins, else auto-discovered. */
static const char *
mt_ld_resolved(void)
{
	const char *e = getenv("MT_LD");
	return e ? e : mt_ld_auto;
}

/* Convert mcc target triplet to the target name expected by mt tools.
 * Returns NULL for null/unknown triplet (means "host arch", which mt
 * defaults to x86_64). */
static const char *
mt_target_name(const char *target_triple)
{
	if (!target_triple || !*target_triple)
		return NULL;
	if (strncmp(target_triple, "x86_64", 6) == 0)
		return "x86_64";
	if (strncmp(target_triple, "aarch64", 7) == 0)
		return "aarch64";
	if (strncmp(target_triple, "riscv64", 7) == 0)
		return "riscv64";
	if (strncmp(target_triple, "loongarch64", 11) == 0)
		return "loongarch64";
	if (strncmp(target_triple, "i386", 4) == 0 ||
	    strncmp(target_triple, "i486", 4) == 0 ||
	    strncmp(target_triple, "i586", 4) == 0 ||
	    strncmp(target_triple, "i686", 4) == 0)
		return "i386";
	if (strncmp(target_triple, "arm", 3) == 0)
		return "arm";
	/* Unknown triplet: return NULL to let caller fall back to host cc. */
	return NULL;
}

/* mt/as now supports all 6 architectures (x86_64, aarch64, riscv64,
 * loongarch64, i386, arm).  A NULL target means "host" (x86_64).  Only return
 * false when the triplet is genuinely unknown. */
static bool
mt_target_supported(const char *target_triple)
{
	if (target_triple == NULL)
		return true;
	return mt_target_name(target_triple) != NULL;
}

/* Assemble a single .s to a .o via mt/as.  mt/as takes the output path
 * explicitly via -o and does not honour a -c flag.  target_triple is
 * passed as --target= so mt/as uses the correct architecture. */
static void
run_mt_as(const char *asm_path, const char *output, bool verbose,
          const char *target_triple)
{
	struct array cmd = {0};
	const char *as = mt_as_resolved();
	const char *mt_target = mt_target_name(target_triple);

	cmdadd(&cmd, as);
	arrayaddbuf(&cmd, " --target=", 10);
	cmdadd(&cmd, mt_target ? mt_target : "x86_64");
	/* Pass -march= so mt/as enforces the same ISA level mcc selected
	 * (as-isa-gating).  Skipped when the level is baseline. */
	{
		const char *march =
			mt_march_for_features(mt_target ? mt_target : "x86_64",
			                      g_target_features);
		if (march) {
			arrayaddbuf(&cmd, " --march=", 10);
			cmdadd(&cmd, march);
		}
	}
	arrayaddbuf(&cmd, " -o ", 4);
	cmdadd(&cmd, output);
	arrayaddbuf(&cmd, " ", 1);
	cmdadd(&cmd, asm_path);
	arrayaddbuf(&cmd, "", 1);
	if (verbose)
		fprintf(stderr, "%s\n", (char *)cmd.val);
	if (system((char *)cmd.val) != 0)
		fatal("mt/as failed");
}

/* Link one or more .o/.a inputs via mt/ld.  Reuses the libdirs/libs
 * traversal and the crt1/libc-meuos/atomic logic of run_host_cc so the
 * link command shape matches what cc would have produced.
 *
 * asm_path_for_atomic, when non-NULL, is the .s that was just
 * assembled; it is scanned for atomic RMW symbols to decide whether
 * -latomic-meuos is needed.  run_host_link passes NULL because the
 * atomic decision was already made when each .o was produced. */
static void
run_mt_ld(struct array *objects, const char *output, bool verbose,
    struct array *libdirs, struct array *libs, bool static_link,
    bool shared, bool pie, bool nostdlib, bool nodefaultlibs, bool meuos_specs,
    const char *asm_path_for_atomic, const char *target_triple)
{
	struct array cmd = {0};
	const char *ld = mt_ld_resolved();
	const char *sysroot;
	const char *mt_target = mt_target_name(target_triple);
	char **p;
	size_t i;

	cmdadd(&cmd, ld);
	arrayaddbuf(&cmd, " --target=", 10);
	cmdadd(&cmd, mt_target ? mt_target : "x86_64");
	/* mt/ld is static by default; -static is accepted for compatibility. */
	if (static_link)
		arrayaddbuf(&cmd, " -static", 8);
	/* -shared triggers ET_DYN output for shared libraries. */
	if (shared)
		arrayaddbuf(&cmd, " -shared", 8);
	/* -pie triggers PIE output (ET_DYN + PT_INTERP).  mt/ld defaults to
	 * ET_EXEC when neither -shared nor -pie is given. */
	if (pie && !static_link && !shared)
		arrayaddbuf(&cmd, " -pie", 5);
	/* MEUOS_SYSROOT (possibly arch-resolved by resolve_arch_sysroot) is
	 * exposed to mt/ld via --sysroot so the linker searches
	 * <sysroot>/usr/lib in addition to the explicit -L paths.  The -L
	 * paths from the driver remain authoritative.  Prefer the driver's
	 * resolved sysroot over the raw env var so arch-specific paths
	 * (sysroot/<arch>/) work for cross-compilation. */
	sysroot = driver_sysroot ? driver_sysroot : getenv("MEUOS_SYSROOT");
	if (sysroot) {
		arrayaddbuf(&cmd, " --sysroot=", 11);
		cmdadd(&cmd, sysroot);
	}
	for (i = 0, p = libdirs->val; i < libdirs->len / sizeof(char *); ++i) {
		arrayaddbuf(&cmd, " -L", 3);
		cmdadd(&cmd, p[i]);
	}
	arrayaddbuf(&cmd, " -o ", 4);
	cmdadd(&cmd, output);
	/* Object inputs must precede libraries for left-to-right link order. */
	for (i = 0, p = objects->val; i < objects->len / sizeof(char *); ++i) {
		arrayaddbuf(&cmd, " ", 1);
		cmdadd(&cmd, p[i]);
	}
	/* crt1.o provides _start; needed when nostdlib+meuos_specs select the
	 * MeuOS CRT instead of the host startup objects.
	 * Shared libraries have no _start entry point. */
	if (meuos_specs && nostdlib && !shared)
		arrayaddbuf(&cmd, " -l:crt1.o", 10);
	for (i = 0, p = libs->val; i < libs->len / sizeof(char *); ++i) {
		arrayaddbuf(&cmd, " -l", 3);
		cmdadd(&cmd, p[i]);
	}
	if (meuos_specs && !nodefaultlibs && !shared)
		arrayaddbuf(&cmd, " -lc-meuos", 10);
	/* The sysroot's libgcc-meuos.a supplies libgcc-ABI soft helpers
	 * (__divdi3, __udivdi3, __ctzdi2, ...).  As an archive it contributes
	 * nothing unless an emitted instruction sequence references a helper, so
	 * pulling it on every MeuOS link is harmless for code mcc inlines
	 * natively; it only kicks in for wide/soft ops without native forms. */
	if (meuos_specs && !nodefaultlibs && !shared && sysroot_has_libgcc())
		arrayaddbuf(&cmd, " -lgcc-meuos", 12);
	/* Atomic runtime: same detection as run_host_cc. */
	if (!nostdlib && !nodefaultlibs && asm_path_for_atomic &&
	    asm_requires_atomic(asm_path_for_atomic))
		arrayaddbuf(&cmd, " -latomic-meuos", 15);
	arrayaddbuf(&cmd, "", 1);
	if (verbose)
		fprintf(stderr, "%s\n", (char *)cmd.val);
	if (system((char *)cmd.val) != 0)
		fatal("mt/ld failed");
}

/* Hand the emitted assembly to the host toolchain.
 * - compile_only: `cc -c` (assemble to .o, no link)
 * - otherwise:    `cc` (assemble + link)
 * Link flags (-L/-l/-static/-nostdlib) apply only when linking. */
void
run_host_cc(const char *asm_path, const char *output, bool compile_only,
            bool verbose, struct array *libdirs, struct array *libs,
            bool static_link, bool shared, bool pie, bool nostdlib, bool nodefaultlibs,
            bool meuos_specs, const char *target_triple,
            struct array *wa_args, struct array *wl_args)
{
	struct array cmd = {0};
	const char *cc = pick_host_cc(target_triple);
	char **p;
	size_t i;

	/* P3: mt integration.  Bypass the host cc when MT_AS/MT_LD are set
	 * and the target is supported (all 5 architectures: x86_64, aarch64,
	 * riscv64, loongarch64, i386).  mt/ld now supports -shared for shared
	 * libraries as well (ET_DYN with .dynsym/.dynstr/.hash).
	 *
	 * -c (assemble only): mt/as needs no CRT/libc, so any MT-enabled
	 *   build assembles via mt/as.
	 * full link: mt/ld has no startup objects or libc of its own; it
	 *   only works when mcc manages the CRT/libc via --specs=meuos.
	 *   Without specs the host cc still provides the host libc/CRT, so
	 *   fall back to it (this preserves `mcc hello.c -o hello` and the
	 *   `make check` smoke link). */
	if (mt_mode_enabled() &&
	    mt_target_supported(target_triple)) {
		if (compile_only) {
			run_mt_as(asm_path, output, verbose, target_triple);
			return;
		}
		if (meuos_specs) {
			/* mt/ld does not accept .s input: assemble to a temp .o
			 * first, then link.  The temp file is removed in both
			 * success and failure paths (fatal() does not return,
			 * so unlink before the link call is unsafe; rely on
			 * the OS cleaning /tmp on exit if the link fails). */
			char tmpl[] = "/tmp/mtccXXXXXX";
			int fd = mkstemp(tmpl);
			if (fd < 0)
				fatal("mkstemp:");
			close(fd);
			run_mt_as(asm_path, tmpl, verbose, target_triple);
			struct array objs = {0};
			arrayaddptr(&objs, tmpl);
			run_mt_ld(&objs, output, verbose, libdirs, libs,
			    static_link, shared, pie, nostdlib, nodefaultlibs, meuos_specs,
			    asm_path, target_triple);
			unlink(tmpl);
			return;
		}
	}

	cmdadd(&cmd, cc);
	if (target_is_i386(target_triple))
		arrayaddbuf(&cmd, " -m32", 5);
	/* mkstemp has no filename suffix. Tell the host driver explicitly that
	 * the generated temporary is assembler, rather than relying on .s. */
	arrayaddbuf(&cmd, " -x assembler ", 14);
	cmdadd(&cmd, asm_path);
	if (compile_only)
		arrayaddbuf(&cmd, " -c", 3);
	for (i = 0, p = libdirs->val; i < libdirs->len / sizeof(char *); ++i) {
		arrayaddbuf(&cmd, " -L", 3);
		cmdadd(&cmd, p[i]);
	}
	if (static_link)
		arrayaddbuf(&cmd, " -static", 8);
	if (shared)
		arrayaddbuf(&cmd, " -shared", 8);
	if (pie && !static_link && !shared)
		arrayaddbuf(&cmd, " -pie", 5);
	if (nostdlib)
		arrayaddbuf(&cmd, " -nostdlib", 10);
	if (nodefaultlibs)
		arrayaddbuf(&cmd, " -nodefaultlibs", 15);
	if (!compile_only && meuos_specs && nostdlib)
		arrayaddbuf(&cmd, " -l:crt1.o", 10);
	/* CRT objects may reference libc symbols (for example the TLS setup
	 * routine), so they must precede every archive.  Keep user libraries
	 * after crt1 as conventional link order requires. */
	for (i = 0, p = libs->val; i < libs->len / sizeof(char *); ++i) {
		arrayaddbuf(&cmd, " -l", 3);
		cmdadd(&cmd, p[i]);
	}
	/* MeuOS specs select the sysroot's libc explicitly.  It is placed before
	 * the atomic runtime so libc objects that need atomics can be resolved by
	 * the following archive in one left-to-right link pass. */
	if (!compile_only && meuos_specs && !nodefaultlibs)
		arrayaddbuf(&cmd, " -lc-meuos", 10);
	if (!compile_only && meuos_specs && !nodefaultlibs && sysroot_has_libgcc())
		arrayaddbuf(&cmd, " -lgcc-meuos", 12);
	/* Atomic RMW expressions lower to the portable libatomic ABI.  Host
	 * bootstrap links need this library even for widths that the host compiler
	 * would normally inline itself.  A MeuOS sysroot supplies this ABI. */
	if (!compile_only && !nostdlib && !nodefaultlibs &&
	    asm_requires_atomic(asm_path))
		arrayaddbuf(&cmd, meuos_specs ? " -latomic-meuos" : " -latomic",
		            meuos_specs ? 15 : 9);
	/* -Wa,<args> / -Wl,<args> passthrough: forwarded verbatim to the
	 * host assembler/linker driver. */
	for (i = 0, p = wa_args->val; i < wa_args->len / sizeof(char *); ++i) {
		arrayaddbuf(&cmd, " -Wa,", 5);
		cmdadd(&cmd, p[i]);
	}
	for (i = 0, p = wl_args->val; i < wl_args->len / sizeof(char *); ++i) {
		arrayaddbuf(&cmd, " -Wl,", 5);
		cmdadd(&cmd, p[i]);
	}
	arrayaddbuf(&cmd, " -o ", 4);
	cmdadd(&cmd, output);
	arrayaddbuf(&cmd, "", 1);  /* NUL terminator */
	if (verbose)
		fprintf(stderr, "%s\n", (char *)cmd.val);
	if (system((char *)cmd.val) != 0)
		fatal("host assembler/linker failed");
}

/* Link already assembled inputs.  This is deliberately separate from
 * run_host_cc(): feeding an object to the frontend makes it parse ELF bytes
 * as C source, which breaks the final link of a self-host rebuild. */
void
run_host_link(struct array *objects, const char *output, bool verbose,
	struct array *libdirs, struct array *libs, bool static_link, bool shared,
	bool pie, bool nostdlib, bool nodefaultlibs, bool meuos_specs,
	const char *target_triple, struct array *wl_args)
{
	struct array cmd = {0};
	const char *cc = pick_host_cc(target_triple);
	char **p;
	size_t i;

	/* P3: mt integration.  Drive mt/ld directly for any supported target
	 * under --specs=meuos (the only mode where mcc manages crt1.o and
	 * libc-meuos).  mt/ld now supports -shared (ET_DYN) and -pie (PIE),
	 * so both static and shared links go through mt/ld. */
	if (mt_mode_enabled() &&
	    mt_target_supported(target_triple) && meuos_specs) {
		run_mt_ld(objects, output, verbose, libdirs, libs, static_link,
		    shared, pie, nostdlib, nodefaultlibs, meuos_specs, NULL,
		    target_triple);
		return;
	}

	cmdadd(&cmd, cc);
	if (target_is_i386(target_triple))
		arrayaddbuf(&cmd, " -m32", 5);
	for (i = 0, p = objects->val; i < objects->len / sizeof(char *); ++i) {
		arrayaddbuf(&cmd, " ", 1);
		cmdadd(&cmd, p[i]);
	}
	for (i = 0, p = libdirs->val; i < libdirs->len / sizeof(char *); ++i) {
		arrayaddbuf(&cmd, " -L", 3);
		cmdadd(&cmd, p[i]);
	}
	if (static_link)
		arrayaddbuf(&cmd, " -static", 8);
	if (shared)
		arrayaddbuf(&cmd, " -shared", 8);
	if (pie && !static_link && !shared)
		arrayaddbuf(&cmd, " -pie", 5);
	if (nostdlib)
		arrayaddbuf(&cmd, " -nostdlib", 10);
	if (nodefaultlibs)
		arrayaddbuf(&cmd, " -nodefaultlibs", 15);
	if (meuos_specs && nostdlib)
		arrayaddbuf(&cmd, " -l:crt1.o", 10);
	for (i = 0, p = libs->val; i < libs->len / sizeof(char *); ++i) {
		arrayaddbuf(&cmd, " -l", 3);
		cmdadd(&cmd, p[i]);
	}
	if (meuos_specs && !nodefaultlibs)
		arrayaddbuf(&cmd, " -lc-meuos", 10);
	if (meuos_specs && !nodefaultlibs && sysroot_has_libgcc())
		arrayaddbuf(&cmd, " -lgcc-meuos", 12);
	/* -Wl,<args> passthrough: forwarded verbatim to the host linker. */
	for (i = 0, p = wl_args->val; i < wl_args->len / sizeof(char *); ++i) {
		arrayaddbuf(&cmd, " -Wl,", 5);
		cmdadd(&cmd, p[i]);
	}
	arrayaddbuf(&cmd, " -o ", 4);
	cmdadd(&cmd, output);
	arrayaddbuf(&cmd, "", 1);
	if (verbose)
		fprintf(stderr, "%s\n", (char *)cmd.val);
	if (system((char *)cmd.val) != 0)
		fatal("host linker failed");
}

bool
is_link_input(const char *path)
{
	size_t n = strlen(path);

	return (n > 2 && strcmp(path + n - 2, ".o") == 0) ||
	       (n > 2 && strcmp(path + n - 2, ".a") == 0) ||
	       (n > 3 && strcmp(path + n - 3, ".so") == 0);
}

/* Derive a default output basename: strip directory and replace the
 * suffix of the first input with `ext` (e.g. "foo.c" + ".o" -> "foo.o"). */
char *
default_out_name(const char *input, const char *ext)
{
	const char *base, *dot;
	char *out;
	size_t stem;

	if (!input)
		input = "stdin";
	base = strrchr(input, '/');
	base = base ? base + 1 : input;
	dot = strrchr(base, '.');
	stem = dot ? (size_t)(dot - base) : strlen(base);
	out = xmalloc(stem + strlen(ext) + 1);
	memcpy(out, base, stem);
	strcpy(out + stem, ext);
	return out;
}
