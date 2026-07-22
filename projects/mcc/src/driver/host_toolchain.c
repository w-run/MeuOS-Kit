/* host_toolchain.c - hand emitted assembly / objects to the host
 * assembler and linker. This is a Phase-1 bootstrap convenience; later
 * phases replace the host `cc` with a self-hosted path.
 *
 * Cross-file entries: sysrootpath(), run_host_cc(), run_host_link(),
 * is_link_input(), default_out_name(). File-local: cmdadd(),
 * asm_requires_atomic(). */
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
