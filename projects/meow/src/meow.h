/* meow - MeuOS build system: shared declarations.
 *
 * The build system was originally a single meow.c; it is split by function:
 *
 *   meow.h       types, constants, global state, cross-file prototypes
 *   state.c      global state definitions
 *   exec.c       run() command executor + list_packages()
 *   recipe.c     load_recipe() + root-level include + text helpers
 *   parse.c      parse_recipe() + env/target-table/list helpers
 *   graph.c      run_target() + out-of-date + command expansion
 *   main.c       main() entry point, arg dispatch, Makefile/bootstrap compat
 *
 * Global state (recipe_environment, targets[], ntargets, default_target,
 * parallel_jobs) lives in state.c and is declared extern here so every
 * module can read/write it without re-declaration. */
#ifndef MEOW_H
#define MEOW_H

#include <stddef.h>
#include <sys/types.h>

#define RECIPE_MAX 65536
#define RECIPE_ENV_MAX 4096
#define TARGET_MAX 128
#define TARGET_DEPS_MAX 64
#define TARGET_COMMANDS_MAX 64

struct target {
	char *name;
	char *deps[TARGET_DEPS_MAX];
	size_t ndeps;
	char *commands[TARGET_COMMANDS_MAX];
	size_t ncommands;
	char *inputs[TARGET_DEPS_MAX];
	size_t ninputs;
	char *outputs[TARGET_DEPS_MAX];
	size_t noutputs;
	int phony;
	int visiting;
	int done;
	char *stem;
};

/* Global build state (defined in state.c). */
extern char recipe_environment[RECIPE_ENV_MAX];
extern struct target targets[TARGET_MAX];
extern size_t ntargets;
extern char *default_target;
extern int parallel_jobs;
extern char *build_arch;  /* NULL = auto-detect from uname */

/* exec.c */
int list_packages(void);
int run(const char *command);

/* recipe.c */
int load_recipe(const char *package, char *path, size_t path_size, char *data);

/* parse.c - only find_target() and parse_recipe() are cross-file. */
struct target *find_target(const char *name);
int parse_recipe(char *data);

/* graph.c */
int run_target(struct target *target);

/* probe.c — inline feature detection (autoconf replacement) */
void probe_reset(void);
int probe_add_header(const char *name);
int probe_add_function(const char *name);
int probe_set_cc(const char *cc);
int probe_set_cflags(const char *cflags);
int probe_set_config(const char *name);
int probe_run(const char *build_dir);

#endif /* MEOW_H */
