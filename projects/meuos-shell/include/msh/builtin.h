/* msh/builtin.h — 内建命令接口（骨架 stub） */
#ifndef MEUOS_MSH_BUILTIN_H
#define MEUOS_MSH_BUILTIN_H

#include "msh/msh.h"

/* 检查 name 是否是 msh 内建命令。若是，返回 1，函数指针通过 *fn 传出。
 * 若否，返回 0。骨架阶段无内建，留作 P6 阶段实现。 */
int msh_is_builtin(const char *name, int (**fn)(int argc, char **argv));

#endif /* MEUOS_MSH_BUILTIN_H */
