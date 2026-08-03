/* msh/main/i18n.c — 国际化框架
 *
 * 支持 zh-CN / en-US 双语。
 * 通过 MSH_LANG 或 LANG 环境变量选择语言。
 * 消息键 → 翻译字符串的静态映射。
 *
 * 用法：
 *   msh_i18n(MSG_HELLO)  → "你好" (zh-CN) / "Hello" (en-US)
 *   msh_i18n_f(MSG_FMT_COUNT, 3) → "共 3 项" / "3 items"
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include "msh/i18n.h"

/* 当前语言 */
static msh_lang_t g_lang = MSH_LANG_ZH_CN;
static int g_i18n_initialized = 0;

/* 消息键 → [zh-CN, en-US] 映射表 */
typedef struct {
    msh_msg_t key;
    const char *zh;
    const char *en;
} msg_entry_t;

static const msg_entry_t msg_table[] = {
    /* === 启动/版本 === */
    { MSG_SHELL_START,     "msh %s（输入 'exit' 退出）\n",     "msh %s (type 'exit' to quit)\n" },
    { MSG_VERSION_LINE,   "%s (MeuOS Shell) %s\n",             "%s (MeuOS Shell) %s\n" },
    { MSG_COPYRIGHT,      "Copyright (C) 2026 MeuOS Kit 贡献者\n",  "Copyright (C) 2026 MeuOS Kit contributors\n" },
    { MSG_LICENSE,        "许可证: %s\n",                       "License: %s\n" },
    { MSG_FEATURES,       "POSIX sh + bash 兼容 + zsh 插件 + YAML 配置\n",  "POSIX sh + bash compat + zsh plugins + YAML config\n" },
    { MSG_BUILT_WITH,     "构建工具: mcc + meuos-libc + meuos-toolchain\n", "Built with: mcc + meuos-libc + meuos-toolchain\n" },

    /* === 用法/帮助 === */
    { MSG_USAGE_HEADER,   "用法: %s [选项] [命令 | 脚本]\n\n",  "Usage: %s [OPTIONS] [COMMAND | SCRIPT]\n\n" },
    { MSG_USAGE_C,        "  -c 命令       执行命令并退出\n",   "  -c COMMAND    Run COMMAND and exit\n" },
    { MSG_USAGE_I,        "  -i            交互式 REPL（默认）\n", "  -i            Interactive REPL (default)\n" },
    { MSG_USAGE_RC,       "  --rc 文件     加载配置文件\n",      "  --rc FILE     Load config file\n" },
    { MSG_USAGE_NOPROFILE,"  --noprofile   不加载用户配置\n",    "  --noprofile    Don't load user config\n" },
    { MSG_USAGE_POSIX,    "  --posix       严格 POSIX 模式\n",   "  --posix        Strict POSIX mode\n" },
    { MSG_USAGE_CLASSIC,  "  --classic     经典兼容模式\n",     "  --classic      Classic compatibility mode\n" },
    { MSG_USAGE_HELP,     "      --help    显示帮助并退出\n",   "      --help    Display this help and exit\n" },
    { MSG_USAGE_VERSION,  "      --version 输出版本并退出\n",   "      --version Output version information and exit\n" },
    { MSG_USAGE_TAIL,     "未指定 -c 或脚本时进入交互模式。\n",  "If neither -c nor a SCRIPT is given, msh enters interactive mode.\n" },
    { MSG_TRY_HELP,       "%s: 使用 --help 获取更多信息\n",     "%s: try --help for more information\n" },

    /* === 错误 === */
    { MSG_ERR_CMD_NOT_FOUND, "%s: %s: 未找到命令\n",            "%s: %s: command not found\n" },
    { MSG_ERR_SYNTAX,        "%s: 语法错误: %s\n",              "%s: syntax error: %s\n" },
    { MSG_ERR_CANNOT_OPEN,   "%s: 无法打开 %s: %s\n",          "%s: cannot open %s: %s\n" },
    { MSG_ERR_NO_SUCH_FILE,  "%s: %s: 没有那个文件或目录\n",    "%s: %s: No such file or directory\n" },
    { MSG_ERR_PERM_DENIED,   "%s: %s: 权限不够\n",             "%s: %s: Permission denied\n" },
    { MSG_ERR_TOO_MANY_ARGS, "%s: 参数过多\n",                  "%s: too many arguments\n" },
    { MSG_ERR_MISSING_ARG,   "%s: 缺少参数\n",                  "%s: missing operand\n" },

    /* === 内建命令 === */
    { MSG_BUILTIN_CD_USAGE,  "cd: 用法: cd [目录]\n",          "cd: usage: cd [dir]\n" },
    { MSG_BUILTIN_CD_HOME,   "cd: 已到家目录\n",               "cd: changed to home directory\n" },
    { MSG_BUILTIN_EXPORT_USAGE, "export: 用法: export NAME[=VALUE]\n", "export: usage: export NAME[=VALUE]\n" },
    { MSG_BUILTIN_SET_USAGE, "set: 用法: set [-e] [+e] [-o option]\n", "set: usage: set [-e] [+e] [-o option]\n" },

    /* === 插件/主题 === */
    { MSG_PLUGIN_LIST_HEADER, "可用插件:\n",                    "Available plugins:\n" },
    { MSG_PLUGIN_NONE,       "  （在 %s 中未找到）\n",          "  (none found in %s)\n" },
    { MSG_PLUGIN_LOADED,     "插件 '%s' 已加载\n",              "Plugin '%s' loaded\n" },
    { MSG_PLUGIN_DISABLED,   "插件 '%s' 已禁用（重启 shell 生效）\n", "Plugin '%s' disabled (reload shell to take effect)\n" },
    { MSG_PLUGIN_NOT_FOUND,  "msh: 未找到插件: %s\n",           "msh: plugin not found: %s\n" },
    { MSG_THEME_LIST_HEADER, "可用主题:\n",                     "Available themes:\n" },
    { MSG_THEME_NONE,        "  （在 %s 中未找到）\n",          "  (none found in %s)\n" },
    { MSG_THEME_APPLIED,     "主题 '%s' 已应用\n",              "Theme '%s' applied\n" },
    { MSG_THEME_NOT_FOUND,   "msh: 未找到主题: %s\n",           "msh: theme not found: %s\n" },
    { MSG_THEME_CURRENT,     "当前主题: %s\n",                  "Current theme: %s\n" },
    { MSG_THEME_NONE_LOADED, "未加载主题（使用默认）\n",        "No theme loaded (using default)\n" },
    { MSG_THEME_UNKNOWN,     "msh: 未知内置主题: %s\n",         "msh: unknown built-in theme: %s\n" },
    { MSG_THEME_AVAILABLE,   "可用: modern, minimal, colorful, powerline\n",  "Available: modern, minimal, colorful, powerline\n" },

    /* === 补全 === */
    { MSG_COMPLETE_NA,       "msh: 无补全候选\n",               "msh: no completions\n" },
};

static const int msg_count = sizeof(msg_table) / sizeof(msg_table[0]);

/* 初始化：从环境变量检测语言 */
void msh_i18n_init(void) {
    if (g_i18n_initialized) return;
    g_i18n_initialized = 1;

    /* 优先 MSH_LANG */
    const char *lang = getenv("MSH_LANG");
    if (!lang || !*lang) lang = getenv("LANG");
    if (!lang || !*lang) lang = getenv("LC_ALL");
    if (!lang || !*lang) {
        g_lang = MSH_LANG_ZH_CN;  /* 默认中文 */
        return;
    }

    /* 匹配语言代码 */
    if (strncasecmp(lang, "zh", 2) == 0) {
        g_lang = MSH_LANG_ZH_CN;
    } else if (strncasecmp(lang, "en", 2) == 0) {
        g_lang = MSH_LANG_EN_US;
    } else {
        g_lang = MSH_LANG_EN_US;  /* 未知语言回退英文 */
    }
}

/* 获取当前语言 */
msh_lang_t msh_i18n_lang(void) {
    if (!g_i18n_initialized) msh_i18n_init();
    return g_lang;
}

/* 设置语言 */
void msh_i18n_set_lang(msh_lang_t lang) {
    g_lang = lang;
    g_i18n_initialized = 1;
}

/* 获取语言名称 */
const char *msh_i18n_lang_name(msh_lang_t lang) {
    switch (lang) {
    case MSH_LANG_ZH_CN: return "zh-CN";
    case MSH_LANG_EN_US: return "en-US";
    default: return "en-US";
    }
}

/* 核心查询：消息键 → 翻译字符串 */
const char *msh_i18n(msh_msg_t key) {
    if (!g_i18n_initialized) msh_i18n_init();

    for (int i = 0; i < msg_count; i++) {
        if (msg_table[i].key == key) {
            return (g_lang == MSH_LANG_ZH_CN) ? msg_table[i].zh : msg_table[i].en;
        }
    }
    return "[?]";  /* 未知消息键 */
}

/* 带格式化的查询 */
void msh_i18n_f(msh_msg_t key, ...) {
    const char *fmt = msh_i18n(key);
    va_list ap;
    va_start(ap, key);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/* 列出所有支持的语言 */
void msh_i18n_list_langs(void) {
    printf("zh-CN  简体中文\n");
    printf("en-US  English (US)\n");
}
