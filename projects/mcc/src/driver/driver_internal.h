/* mcc/driver - internal declarations shared by main.c, target_select.c,
 * host_toolchain.c and usage.c.
 *
 * The driver was a single main.c (736 lines); it is split by function:
 *   main.c            entry point + argument parsing + orchestration
 *   target_select.c   -target triplet -> Target* / canonical name mapping
 *   host_toolchain.c   host assembler/linker handoff (cc -c / cc link)
 *   usage.c            --version / --help text
 *
 * Globals `T` and `debug` are defined in main.c and declared extern in
 * ir.h. The per-arch Target objects below are defined in src/target/<arch>/. */
#ifndef MCC_DRIVER_INTERNAL_H
#define MCC_DRIVER_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include "util.h"
#include "ir.h"

#define MCC_VERSION "0.1.0"

/* Per-arch IR backend targets (defined in src/target/<arch>/<arch>_targ.c). */
extern Target T_amd64_sysv;
extern Target T_arm64;
extern Target T_rv64;
extern Target T_la64;
extern Target T_i386_sysv;
extern Target T_arm32;

/* target_select.c */
Target *pick_target(const char *triplet);
const char *targ_name(const char *triplet);

/* host_toolchain.c */
char *sysrootpath(const char *root, const char *suffix);
void run_host_cc(const char *asm_path, const char *output, bool compile_only,
                 bool verbose, struct array *libdirs, struct array *libs,
                 bool static_link, bool shared, bool nostdlib,
                 bool nodefaultlibs, bool meuos_specs, const char *target_triple);
void run_host_link(struct array *objects, const char *output, bool verbose,
                   struct array *libdirs, struct array *libs, bool static_link,
                   bool shared, bool nostdlib, bool nodefaultlibs,
                   bool meuos_specs, const char *target_triple);
bool is_link_input(const char *path);
char *default_out_name(const char *input, const char *desc);

/* msys.c — .msys single-file sysroot support */
#include "mt/msys.h"
int msys_is_sysroot(const char *path);
struct msys *msys_sysroot_open(const char *sysroot_path);
char *msys_sysroot_get_arch(struct msys *m);
int  msys_sysroot_incprefixes(const char *prefixes[], int max_count);
/* Global msys handle and path exposed to pp.c for VFS include fallback. */
extern struct msys *msys_sysroot_handle;
extern const char *msys_sysroot_path;

/* usage.c */
void print_version(void);
void usage(void);
void usage_long(void);

#endif /* MCC_DRIVER_INTERNAL_H */
