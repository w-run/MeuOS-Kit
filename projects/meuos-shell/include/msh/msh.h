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

/* === 兼容模式与配置 ===
 * 3 路递进：--classic argv > MSH_CLASSIC env > YAML features.classic
 * classic 模式下：关颜色、不读 rc、不展别名、朴素 $ 提示符。
 */
extern int msh_mode_classic;
extern int msh_errexit;    /* set -e */
extern int msh_pipefail;   /* set -o pipefail */
extern int msh_in_cond;    /* evaluating condition (exempt from errexit) */

/* compctl: completion script system */
int msh_builtin_complete(int argc, char **argv);
int msh_builtin_compgen(int argc, char **argv);
int msh_compctl_lookup(const char *cmd, int *type, const char **func);
int msh_compctl_load_dir(const char *dirpath);

/* 别名表：msh_alias_add / msh_alias_lookup */
void msh_alias_add(const char *name, const char *value);
const char *msh_alias_lookup(const char *name);

/* PS1 提示符（支持 \u \w \h \$ \n 转义）。返回 malloc 字符串。 */
char *msh_prompt_expand(const char *ps1);

/* 加载 YAML 配置（~/.config/msh/config.yaml）。启动时调用。 */
void msh_load_config(const char *rcfile);

/* 一次性执行字符串（公开给 trap/eval 使用）。 */
int msh_run_string(const char *s, size_t len);

/* === trap 内建支持 === */
/* trap 内建命令。返回 0 成功，2 用法错误。 */
int msh_builtin_trap(int argc, char **argv);
/* 检查并执行 pending trap。每次读命令前调用。 */
void msh_trap_check(void);
/* 执行 EXIT trap（shell 退出时调用）。 */
void msh_trap_exit(void);

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_MSH_H */
