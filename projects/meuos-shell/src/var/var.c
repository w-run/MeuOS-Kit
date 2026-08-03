/* msh/var/var.c — 局部变量骨架（当前不实现，仅占位） */
#include <stddef.h>
#include "msh/var.h"

typedef struct msh_var_scope msh_var_scope_t;

msh_var_scope_t *msh_var_scope_push(void) { return NULL; }
void msh_var_scope_pop(msh_var_scope_t *s) { (void)s; }
int msh_var_local_set(const char *name, const char *val) { (void)name; (void)val; return 0; }
