/* msh/exec.h — 命令执行器接口（骨架 stub）
 *
 * 骨架阶段：外部命令通过 fork()+execvp() 实现，简单命令执行。
 * 后续扩展管道/重定向/作业控制。
 */
#ifndef MEUOS_MSH_EXEC_H
#define MEUOS_MSH_EXEC_H

#include "msh/msh.h"

/* 执行简单命令。argv 是 NULL 结尾的字符串数组（含 argv[0]）。
 * 返回值：等待子进程退出码。失败返回 -1。 */
int msh_exec_simple(char *const argv[]);

/* 在 PATH 中查找可执行文件。返回第一个匹配项的全路径，
 * 或 NULL。返回的字符串通过 malloc 分配，调用者需 free()。 */
char *msh_which(const char *name);

#endif /* MEUOS_MSH_EXEC_H */
