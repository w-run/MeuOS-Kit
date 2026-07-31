/* msh/var.h — 变量管理接口
 *
 * 当前实现：变量直接通过 getenv/setenv 与 libc 集成。
 * 简化：未来引入 msh 局部变量时再扩展。
 */
#ifndef MEUOS_MSH_VAR_H
#define MEUOS_MSH_VAR_H

#include "msh/msh.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 内建命令入口（builtin.c） */
int msh_builtin_cd(int argc, char **argv);
int msh_builtin_export(int argc, char **argv);
int msh_builtin_unset(int argc, char **argv);
int msh_builtin_set(int argc, char **argv);

/* 扩展：未来实现局部变量栈 */
typedef struct msh_var_scope msh_var_scope_t;
msh_var_scope_t *msh_var_scope_push(void);
void msh_var_scope_pop(msh_var_scope_t *s);
int msh_var_local_set(const char *name, const char *val);

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_MSH_VAR_H */
