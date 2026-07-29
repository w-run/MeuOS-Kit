/* meow global build state.
 * Declared extern in meow.h; defined here so there is exactly one instance. */
#include "meow.h"

char recipe_environment[RECIPE_ENV_MAX];
struct target targets[TARGET_MAX];
size_t ntargets;
char *default_target;
int parallel_jobs = 1;
char *build_arch;  /* NULL = auto-detect */
const char *build_target;  /* full triple from --target=, or NULL */
char recipe_deps[RECIPE_DEPS_MAX][128];
size_t nrecipe_deps;
char *uses[USES_MAX];
size_t nuses;
char *has_tools_stack[HAS_TOOLS_MAX];
size_t nhas_tools_stack;
char *lib_deps_stack[LIB_DEPS_MAX];
size_t nlib_deps_stack;
char cflags_global[1024] = {0};
char ldflags_global[1024] = {0};
