/* msh/msh.h — MeuOS Shell 公共 API
 *
 * 主入口点在 src/main/main.c。设计原则：
 *   - 模块化：lex/parse/exec/builtin/var 各司其职
 *   - POSIX.1-2008 Shell 命令语言为最小目标
 *   - 后续 P6+ 阶段逐步加入 var/expand/redir/flow
 *
 * 阶段规划见 projects/meuos-shell/ARCHITECTURE.md §5。
 */
#ifndef MEUOS_MSH_H
#define MEUOS_MSH_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 版本信息：定义见 src/main/main.c */
extern const char *msh_version;
extern const char *msh_license;

/* 程序名（去除 argv[0] 的目录前缀） */
extern const char *msh_program_name;

void msh_set_program_name(const char *argv0);
void msh_version_print(FILE *fp);
void msh_version_exit(void) __attribute__((noreturn));

/* 错误处理 */
void msh_die(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_MSH_H */
