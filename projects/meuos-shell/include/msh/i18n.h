/* msh/i18n.h — 国际化框架
 *
 * 支持 zh-CN / en-US 双语。
 * 通过 MSH_LANG 环境变量选择语言。
 */
#ifndef MSH_I18N_H
#define MSH_I18N_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MSH_LANG_ZH_CN = 0,
    MSH_LANG_EN_US = 1,
} msh_lang_t;

/* 消息键枚举 */
typedef enum {
    /* 启动/版本 */
    MSG_SHELL_START = 0,
    MSG_VERSION_LINE,
    MSG_COPYRIGHT,
    MSG_LICENSE,
    MSG_FEATURES,
    MSG_BUILT_WITH,

    /* 用法/帮助 */
    MSG_USAGE_HEADER,
    MSG_USAGE_C,
    MSG_USAGE_I,
    MSG_USAGE_RC,
    MSG_USAGE_NOPROFILE,
    MSG_USAGE_POSIX,
    MSG_USAGE_CLASSIC,
    MSG_USAGE_HELP,
    MSG_USAGE_VERSION,
    MSG_USAGE_TAIL,
    MSG_TRY_HELP,

    /* 错误 */
    MSG_ERR_CMD_NOT_FOUND,
    MSG_ERR_SYNTAX,
    MSG_ERR_CANNOT_OPEN,
    MSG_ERR_NO_SUCH_FILE,
    MSG_ERR_PERM_DENIED,
    MSG_ERR_TOO_MANY_ARGS,
    MSG_ERR_MISSING_ARG,

    /* 内建命令 */
    MSG_BUILTIN_CD_USAGE,
    MSG_BUILTIN_CD_HOME,
    MSG_BUILTIN_EXPORT_USAGE,
    MSG_BUILTIN_SET_USAGE,

    /* 插件/主题 */
    MSG_PLUGIN_LIST_HEADER,
    MSG_PLUGIN_NONE,
    MSG_PLUGIN_LOADED,
    MSG_PLUGIN_DISABLED,
    MSG_PLUGIN_NOT_FOUND,
    MSG_THEME_LIST_HEADER,
    MSG_THEME_NONE,
    MSG_THEME_APPLIED,
    MSG_THEME_NOT_FOUND,
    MSG_THEME_CURRENT,
    MSG_THEME_NONE_LOADED,
    MSG_THEME_UNKNOWN,
    MSG_THEME_AVAILABLE,

    /* 补全 */
    MSG_COMPLETE_NA,

    MSG__COUNT  /* sentinel */
} msh_msg_t;

/* 初始化：从环境变量检测语言 */
void msh_i18n_init(void);

/* 获取/设置当前语言 */
msh_lang_t msh_i18n_lang(void);
void msh_i18n_set_lang(msh_lang_t lang);

/* 获取语言名称字符串 */
const char *msh_i18n_lang_name(msh_lang_t lang);

/* 核心查询：消息键 → 翻译字符串 */
const char *msh_i18n(msh_msg_t key);

/* 带格式化的查询（输出到 stderr） */
void msh_i18n_f(msh_msg_t key, ...);

/* 列出所有支持的语言 */
void msh_i18n_list_langs(void);

#ifdef __cplusplus
}
#endif

#endif /* MSH_I18N_H */
