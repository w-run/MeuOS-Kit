/* meuos/syntax.h — 轻量语法着色
 *
 * 设计原则：
 *   - 不做完整语法解析（不用正则或 lex/flex）
 *   - 基于文件扩展名 + 简单 token 规则（关键字、字符串、注释）
 *   - 数据驱动：每种语言一个模式表
 *   - cat 等工具直接用：syntax_highlight_line() 逐行渲染
 */
#ifndef MEUOS_SYNTAX_H
#define MEUOS_SYNTAX_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYN_LANG_UNKNOWN,
    SYN_LANG_C,           /* .c .h */
    SYN_LANG_PYTHON,
    SYN_LANG_SHELL,
    SYN_LANG_RUST,
    SYN_LANG_GO,
    SYN_LANG_JS,
    SYN_LANG_JSON,
    SYN_LANG_YAML,
    SYN_LANG_MARKDOWN,
    SYN_LANG_DIFF,
} syn_language_t;

/* 从文件名推断语言 */
syn_language_t syn_detect(const char *path);

/* 渲染单行（保留原 \n 字符）。
 * outfp 是输出流，通常 stdout。
 * color=1 启用颜色，0 输出纯文本。 */
void syntax_highlight_line(syn_language_t lang, const char *line,
                           size_t len, FILE *outfp, int color);

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_SYNTAX_H */
