/* cpp_newdel.c - placeholder.  The original contents were split into
 * per-domain submodules at Stage C.3.1; this file is kept as a stable
 * include point so Makefile find / IDE indexing / external references
 * (e.g. doc/pm text mentioning `cpp_newdel.c`) continue to resolve.
 *
 * The split (each is now its own .c with the same include set):
 *   - cpp_newdel_expr.c   new/delete expressions + ctor/malloc/free helpers
 *   - cpp_newdel_thunk.c  exception payload thunks (copy/dtor helpers)
 *   - cpp_newdel_exc.c    exception runtime integration + throw + try/catch
 *
 * No logic remains here; all public entry points live in the three
 * sibling files.  Pure refactor, zero behavior change.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "mcc.h"
#include "cpp.h"
#include "cpp_internal.h"
#include "../../c/parse/decl_internal.h"
#include "../../c/parse/expr_internal.h"
