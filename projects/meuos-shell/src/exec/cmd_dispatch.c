/* msh/exec/cmd_dispatch.c — :cmd 指令分发系统
 *
 * :cmd 是 msh 的原生快捷指令系统，受 vim 启发。
 * 用户输入 :foo 时，msh 在 PATH 查找前拦截并分发到 :cmd 系统。
 *
 * 分发优先级：
 *   1. 内置 :cmd（C 实现）
 *   2. 插件注册的函数 _cmd_<name>
 *   3. 别名 :<name>
 *   4. 报错 "unknown :cmd"
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "msh/msh.h"
#include "msh/cmd.h"
#include "msh/i18n.h"
#include "msh/plugin.h"
#include "msh/parse.h"

/* 检查是否为 :cmd 格式 */
int msh_is_colon_cmd(const char *name) {
    return name && name[0] == ':' && name[1] != '\0';
}

/* === 内置 :cmd 表 === */
typedef struct {
    const char *cmd;     /* 不含 : 前缀 */
    const char *desc;
} colon_cmd_t;

static const colon_cmd_t builtin_cmds[] = {
    { "theme",       "应用主题: :theme <name>" },
    { "themes",      "列出所有主题" },
    { "theme-info",  "显示主题详情: :theme-info <name>" },
    { "plugin",      "加载插件: :plugin <name>" },
    { "plugins",     "列出所有插件" },
    { "plugin-info", "显示插件详情: :plugin-info <name>" },
    { "lang",        "切换语言: :lang zh-CN|en-US" },
    { "langs",       "列出支持的语言" },
    { "compat",      "导入 bash/zsh 配置: :compat bash|zsh" },
    { "config",      "显示当前配置" },
    { "reload",      "重新加载配置" },
    { "init-plugin", "创建插件模板: :init-plugin <name>" },
    { "init-theme",  "创建主题模板: :init-theme <name>" },
    { "help",        "显示帮助: :help [cmd]" },
    { "version",     "显示版本信息" },
    { NULL, NULL },
};

/* 尝试调用用户定义的函数 _cmd_<name> */
static int try_user_function(const char *cmd_name, int argc, char **argv) {
    /* 构建函数名 _cmd_<name> */
    char func_name[128];
    snprintf(func_name, sizeof(func_name), "_cmd_%s", cmd_name);

    /* 通过 msh_run_string 调用函数，传递参数 */
    /* 构造调用脚本: _cmd_name "$@" */
    /* 但 msh -c 模式下 $@ 可能不正确，简化为直接调用无参函数 */
    char script[256];
    snprintf(script, sizeof(script), "%s", func_name);

    /* 尝试执行函数 */
    int rc = msh_run_string(script, strlen(script));
    /* 如果返回 127，说明函数不存在 */
    return rc;
}

/* 尝试别名 */
static int try_alias(const char *full_name) {
    /* 检查是否有别名 :name */
    extern const char *msh_alias_lookup(const char *name);
    const char *alias_val = msh_alias_lookup(full_name);
    if (alias_val) {
        return msh_run_string(alias_val, strlen(alias_val));
    }
    return 127; /* not found */
}

/* :cmd 分发 */
int msh_cmd_dispatch(int argc, char **argv) {
    const char *full = argv[0];  /* ":cmdname" */
    const char *cmd = full + 1;  /* "cmdname"（跳过 :） */

    /* 提取子命令名（到空格或结束为止，实际 argv 已经拆分好了） */
    /* cmd 现在是去掉 : 的命令名 */

    /* --- 内置命令 --- */

    if (strcmp(cmd, "themes") == 0) {
        return msh_theme_list();
    }
    if (strcmp(cmd, "theme") == 0) {
        if (argc < 2) return msh_theme_list();
        return msh_theme_apply(argv[1]) == 0 ? 0 : 1;
    }
    if (strcmp(cmd, "theme-info") == 0) {
        if (argc < 2) {
            fprintf(stderr, "用法: :theme-info <name>\n");
            return 1;
        }
        /* 先扫描再调用 plugin.c 的 info（通过 theme list 间接） */
        extern int msh_plugin_builtin(int argc, char **argv);
        char *nargv[] = { "msh", "plugin", "theme", "info", argv[1], NULL };
        return msh_plugin_builtin(5, nargv);
    }
    if (strcmp(cmd, "plugins") == 0) {
        msh_plugin_scan();
        return msh_plugin_list();
    }
    if (strcmp(cmd, "plugin") == 0) {
        if (argc < 2) {
            msh_plugin_scan();
            return msh_plugin_list();
        }
        msh_plugin_scan();
        return msh_plugin_enable(argv[1]) == 0 ? 0 : 1;
    }
    if (strcmp(cmd, "plugin-info") == 0) {
        if (argc < 2) {
            fprintf(stderr, "用法: :plugin-info <name>\n");
            return 1;
        }
        msh_plugin_scan();
        extern int msh_plugin_builtin(int argc, char **argv);
        char *nargv[] = { "msh", "plugin", "info", argv[1], NULL };
        return msh_plugin_builtin(5, nargv);
    }
    if (strcmp(cmd, "langs") == 0) {
        msh_i18n_list_langs();
        printf("\n当前: %s\n", msh_i18n_lang_name(msh_i18n_lang()));
        return 0;
    }
    if (strcmp(cmd, "lang") == 0) {
        if (argc < 2) {
            msh_i18n_list_langs();
            printf("\n当前: %s\n", msh_i18n_lang_name(msh_i18n_lang()));
            return 0;
        }
        const char *l = argv[1];
        if (strcmp(l, "zh-CN") == 0 || strcmp(l, "zh") == 0) {
            msh_i18n_set_lang(MSH_LANG_ZH_CN);
            setenv("MSH_LANG", "zh-CN", 1);
            printf("语言已切换: 简体中文\n");
        } else if (strcmp(l, "en-US") == 0 || strcmp(l, "en") == 0) {
            msh_i18n_set_lang(MSH_LANG_EN_US);
            setenv("MSH_LANG", "en-US", 1);
            printf("Language: English (US)\n");
        } else {
            fprintf(stderr, "msh: 不支持的语言: %s\n", l);
            msh_i18n_list_langs();
            return 1;
        }
        return 0;
    }
    if (strcmp(cmd, "compat") == 0) {
        if (argc < 2) {
            fprintf(stderr, "用法: :compat bash|zsh\n");
            return 1;
        }
        return msh_compat_import(argv[1]);
    }
    if (strcmp(cmd, "config") == 0) {
        printf("=== msh 配置 ===\n");
        printf("语言:     %s\n", msh_i18n_lang_name(msh_i18n_lang()));
        const char *theme = getenv("MSH_THEME");
        printf("主题:     %s\n", theme ? theme : "(默认)");
        const char *ps1 = getenv("MSH_PS1");
        printf("PS1:      %s\n", ps1 ? ps1 : "(内置默认)");
        const char *classic = getenv("MSH_CLASSIC");
        printf("经典模式: %s\n", classic ? classic : "关闭");
        const char *home = getenv("HOME");
        printf("配置目录: %s/.config/msh/\n", home ? home : "~");
        printf("插件目录: %s/.msh/plugins/\n", home ? home : "~");
        printf("主题目录: %s/.msh/themes/\n", home ? home : "~");
        return 0;
    }
    if (strcmp(cmd, "reload") == 0) {
        printf("重新加载配置...\n");
        msh_load_config(NULL);
        printf("配置已重新加载\n");
        return 0;
    }
    if (strcmp(cmd, "version") == 0) {
        msh_version_print(stdout);
        return 0;
    }
    if (strcmp(cmd, "init-plugin") == 0) {
        if (argc < 2) {
            fprintf(stderr, "用法: :init-plugin <name>\n");
            return 1;
        }
        extern int msh_plugin_builtin(int argc, char **argv);
        char *nargv[] = { "msh", "plugin", "init", argv[1], NULL };
        return msh_plugin_builtin(4, nargv);
    }
    if (strcmp(cmd, "init-theme") == 0) {
        if (argc < 2) {
            fprintf(stderr, "用法: :init-theme <name>\n");
            return 1;
        }
        extern int msh_plugin_builtin(int argc, char **argv);
        char *nargv[] = { "msh", "plugin", "init-theme", argv[1], NULL };
        return msh_plugin_builtin(4, nargv);
    }
    if (strcmp(cmd, "help") == 0) {
        return msh_cmd_help(argc > 1 ? argv[1] : NULL);
    }

    /* --- 插件扩展：尝试用户函数 --- */
    int rc = try_user_function(cmd, argc, argv);
    if (rc != 127) return rc;

    /* --- 别名 --- */
    rc = try_alias(full);
    if (rc != 127) return rc;

    /* --- 未找到 --- */
    fprintf(stderr, "msh: 未知指令 :%s（输入 :help 查看可用指令）\n", cmd);
    return 127;
}

/* :help [cmd] */
int msh_cmd_help(const char *cmd) {
    if (cmd) {
        /* 去掉可能的 : 前缀 */
        if (cmd[0] == ':') cmd++;
        for (int i = 0; builtin_cmds[i].cmd; i++) {
            if (strcmp(builtin_cmds[i].cmd, cmd) == 0) {
                printf("  :%s\n    %s\n", builtin_cmds[i].cmd, builtin_cmds[i].desc);
                return 0;
            }
        }
        printf("  :%s — 未知指令\n", cmd);
        return 1;
    }

    printf("msh :cmd 指令系统\n");
    printf("==================\n\n");
    printf("内置指令:\n");
    for (int i = 0; builtin_cmds[i].cmd; i++) {
        printf("  :%-14s %s\n", builtin_cmds[i].cmd, builtin_cmds[i].desc);
    }
    printf("\n");
    printf("插件扩展:\n");
    printf("  插件中定义函数 _cmd_<name> 即可注册 :<name> 指令\n");
    printf("  例如: _cmd_status() { echo '系统正常'; }\n");
    printf("  然后输入 :status 即可调用\n\n");
    printf("  也可通过别名注册:\n");
    printf("  alias :status='echo 系统正常'\n");
    return 0;
}
