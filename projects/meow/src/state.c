/* meow global build state.
 * Declared extern in meow.h; defined here so there is exactly one instance. */
#include "meow.h"

char recipe_environment[RECIPE_ENV_MAX];
struct target targets[TARGET_MAX];
size_t ntargets;
char *default_target;
int parallel_jobs = 1;
char *build_arch;  /* NULL = auto-detect */
