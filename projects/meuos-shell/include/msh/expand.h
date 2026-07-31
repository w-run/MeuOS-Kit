/* msh/expand.h - 字符串展开公共 API
 *
 * 由 expand.c 实现，被 parse.c 在 exec 期调用：
 *   - $VAR / ${VAR}              无修饰取值
 *   - $? $0..$9 $@ $* $#         特殊参数
 *   - ${VAR:-default}            未设或空 -> default
 *   - ${VAR:=default}            未设或空 -> 设 VAR=default 返回 default
 *   - ${VAR:+alt}                已设非空 -> alt
 *   - ${VAR:?err}                未设或空 -> 报错并 exit 非零
 *   - ${VAR%suf} / ${VAR%%suf}   去最短/最长后缀（fnmatch）
 *   - ${VAR#pre} / ${VAR##pre}   去最短/最长前缀（fnmatch）
 *   - ${VAR/pat/repl}           首次替换
 *   - ${#VAR}                    长度
 *   - $(...)  `...`              命令替换：fork 子进程执行，回收 stdout 去尾换行
 *   - $((...))                    算术求值（见 arith.c）
 *   - ~                          仅独立 ~ 替换 $HOME（不支持 ~user）
 *
 * 不实现：${VAR//pat/repl} 全局替换、${VAR:i:j} 子串、数组、
 *         ++/--/= +=/= 三元/位运算、glob ** 递归、~user。
 */
#ifndef MEUOS_MSH_EXPAND_H
#define MEUOS_MSH_EXPAND_H

#include <stddef.h>

#include "msh/msh.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 展开一段字符串。返回 malloc 缓冲区（调用者 free）。
 * 不展开 argv：仅做变量/命令替换/算术/tilde。
 * glob 在 argv 层处理，见 parse.c expand_argv。*/
char *msh_expand(const char *s);

/* 算术求值 $((...))：递归下降。
 * 支持 + - * / % ( ) !、$VAR、数字字面量、
 *      == != < > <= >= && ||（返回 0/1）。
 * 不支持 ++ -- = += 三元 位运算。
 * 返回求值结果；err != NULL 时设置错误信息。*/
long msh_arith(const char *expr, const char **err);

/* 命令替换 $(...)：fork 子进程执行 cmd 文本，回收 stdout 去尾换行。
 * 返回 malloc 缓冲区（调用者 free）。失败返回空串。*/
char *msh_cmdsub(const char *cmd);

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_MSH_EXPAND_H */
