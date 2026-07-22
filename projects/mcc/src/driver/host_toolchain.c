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

/* P3: mt integration.  MT_AS and MT_LD together select the MeuOS
 * toolchain handoff.  Either one alone is treated as not-configured so
 * that partially-set environments fall back to the host cc path. */
static bool
mt_mode_enabled(void)
{
	return getenv("MT_AS") != NULL && getenv("MT_LD") != NULL;
}

/* mt/as currently supports only x86_64.  A NULL target means "host",
 * which for the bootstrap is x86_64; any other triplet (i386, aarch64,
 * riscv64, loongarch64, ...) stays on the cc handoff. */
static bool
mt_target_supported(const char *target_triple)
{
	if (target_triple == NULL)
		return true;
	return strncmp(target_triple, "x86_64", 6) == 0;
}

/* Assemble a single .s to a .o via mt/as.  mt/as takes the output path
 * explicitly via -o and does not honour a -c flag. */
static void
run_mt_as(const char *asm_path, const char *output, bool verbose)
{
	struct array cmd = {0};
	const char *as = getenv("MT_AS");

	cmdadd(&cmd, as);
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
    bool nostdlib, bool nodefaultlibs, bool meuos_specs,
    const char *asm_path_for_atomic)
{
	struct array cmd = {0};
	const char *ld = getenv("MT_LD");
	const char *sysroot;
	char **p;
	size_t i;

	cmdadd(&cmd, ld);
	/* mt/ld is static by default; -static is accepted for compatibility. */
	if (static_link)
		arrayaddbuf(&cmd, " -static", 8);
	/* MEUOS_SYSROOT is also exposed to mt/ld via --sysroot so the linker
	 * searches <sysroot>/usr/lib in addition to the explicit -L paths.
	 * The -L paths from the driver remain authoritative. */
	sysroot = getenv("MEUOS_SYSROOT");
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
	 * MeuOS CRT instead of the host startup objects. */
	if (meuos_specs && nostdlib)
		arrayaddbuf(&cmd, " -l:crt1.o", 10);
	for (i = 0, p = libs->val; i < libs->len / sizeof(char *); ++i) {
		arrayaddbuf(&cmd, " -l", 3);
		cmdadd(&cmd, p[i]);
	}
	if (meuos_specs && !nodefaultlibs)
		arrayaddbuf(&cmd, " -lc-meuos", 10);
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
            bool static_link, bool shared, bool nostdlib, bool nodefaultlibs,
            bool meuos_specs, const char *target_triple)
{
	struct array cmd = {0};
	const char *cc = pick_host_cc(target_triple);
	char **p;
	size_t i;

	/* P3: mt integration.  Bypass the host cc when MT_AS/MT_LD are set
	 * and the target is x86_64 (the only arch mt/as supports).  -shared
	 * still needs the host cc because mt/ld does not emit shared objects.
	 *
	 * -c (assemble only): mt/as needs no CRT/libc, so any MT-enabled
	 *   x86_64 build assembles via mt/as.
	 * full link: mt/ld has no startup objects or libc of its own; it
	 *   only works when mcc manages the CRT/libc via --specs=meuos.
	 *   Without specs the host cc still provides the host libc/CRT, so
	 *   fall back to it (this preserves `mcc hello.c -o hello` and the
	 *   `make check` smoke link). */
	if (mt_mode_enabled() && !shared && !target_is_i386(target_triple) &&
	    mt_target_supported(target_triple)) {
		if (compile_only) {
			run_mt_as(asm_path, output, verbose);
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
			run_mt_as(asm_path, tmpl, verbose);
			struct array objs = {0};
			arrayaddptr(&objs, tmpl);
			run_mt_ld(&objs, output, verbose, libdirs, libs,
			    static_link, nostdlib, nodefaultlibs, meuos_specs,
			    asm_path);
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
	/* Atomic RMW expressions lower to the portable libatomic ABI.  Host
	 * bootstrap links need this library even for widths that the host compiler
	 * would normally inline itself.  A MeuOS sysroot supplies this ABI. */
	if (!compile_only && !nostdlib && !nodefaultlibs &&
	    asm_requires_atomic(asm_path))
		arrayaddbuf(&cmd, meuos_specs ? " -latomic-meuos" : " -latomic",
		            meuos_specs ? 15 : 9);
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
	bool nostdlib, bool nodefaultlibs, bool meuos_specs, const char *target_triple)
{
	struct array cmd = {0};
	const char *cc = pick_host_cc(target_triple);
	char **p;
	size_t i;

	/* P3: mt integration.  Drive mt/ld directly for x86_64 static links
	 * under --specs=meuos (the only mode where mcc manages crt1.o and
	 * libc-meuos).  -shared, non-meuos-specs, i386 and other non-x86_64
	 * targets fall back to the host cc. */
	if (mt_mode_enabled() && !shared && !target_is_i386(target_triple) &&
	    mt_target_supported(target_triple) && meuos_specs) {
		run_mt_ld(objects, output, verbose, libdirs, libs, static_link,
		    nostdlib, nodefaultlibs, meuos_specs, NULL);
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
