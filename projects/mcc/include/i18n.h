/* i18n.h — 轻量双语消息目录（en/zh）。
 *
 * 不做 gettext 复杂链路：g_msg_lang 选择语言（0=en 默认，1=zh）。
 * msg_tr()/msg_tr_fix() 按"源英文字符串"在静态目录中查找译文；未收录
 * 的字符串原样返回（优雅降级为英文）。目录 key 是编译器源码里的
 * printf 格式串，占位符（%s %d 等）两侧必须一致。
 *
 * 语言选择优先级：--lang=en|zh 显式 > 环境 LANG/LC_MESSAGES 以 zh*
 * 前缀推断 > 默认 en。g_msg_lang 在 main() 启动时从环境初始化，--lang
 * 解析时覆盖。
 */
#ifndef MCC_I18N_H
#define MCC_I18N_H

extern int g_msg_lang;      /* 0=en（默认），1=zh */

/* 格式串翻译：返回当前语言的等价格式串（未知条目原样返回 en）。 */
const char *msg_tr(const char *fmt);

/* fix-it 提示翻译（如 "add ';' here"）。 */
const char *msg_tr_fix(const char *fix);

/* 诊断结构词：error:/warning:/note:（含冒号，en/zh 随 g_msg_lang）。 */
const char *msg_word_error(void);
const char *msg_word_warning(void);
const char *msg_word_note(void);

/* --explain 修复建议：按 fmt 分类（implicit declaration / unused /
 * type / nodiscard），返回当前语言的建议文本；无匹配返回 NULL。 */
const char *msg_explain(const char *fmt);

#endif /* MCC_I18N_H */
