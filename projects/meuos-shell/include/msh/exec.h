/* msh/exec.h — 命令执行器公共 API
 *
 * 现在：暴露给 main.c 和 parse.c。
 * 通过 parse.c 中的 msh_eval() 完成所有执行。
 */
#ifndef MEUOS_MSH_EXEC_H
#define MEUOS_MSH_EXEC_H

#include "msh/msh.h"
#include "msh/parse.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 在 PATH 中查找可执行文件。返回 malloc 的全路径（找到时）或 NULL。 */
char *msh_which(const char *name);

/* 当前 shell 的最后退出状态（供 $? 展开） */
extern int msh_last_status;

/* shell 主进程 argv（供 $1..$9 展开） */
extern char **msh_argv;

/* 设置退出码 */
void msh_set_exit(int rc);

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_MSH_EXEC_H */
