/* msh/exec/theme_yaml.c — YAML 结构化主题引擎
 *
 * 主题支持两种格式：
 *   1. .msh 脚本格式（简单，直接设置 MSH_PS1）
 *   2. .yaml 结构化格式（强大，支持状态切换/颜色变量/插件依赖/内嵌脚本）
 *
 * === YAML 主题格式 ===
 *
 *   name: modern
 *   desc: 简约现代风格
 *   colorscheme: dark          # dark | light | none
 *   requires:                   # 依赖插件（自动加载）
 *     - git
 *     - exit-code
 *
 *   colors:                     # 颜色变量定义
 *     primary: "1;32"           # 可在 PS1 中用 ${primary} 引用
 *     secondary: "34"
 *     error: "1;31"
 *     accent: "36"
 *     muted: "90"
 *     warn: "33"
 *
 *   states:                     # 状态机：根据上下文选择不同 PS1
 *     default:                  # 默认状态
 *       ps1: '\e[${primary}m➜\e[0m \e[${secondary}m\w\e[0m\g \$ '
 *       ps2: '\e[${muted}m> \e[0m'
 *     error:                    # 上条命令失败时
 *       ps1: '\e[${error}m➜\e[0m \e[${secondary}m\w\e[0m\g \$ '
 *     root:                     # root 用户时
 *       ps1: '\e[${error}m➜\e[0m \e[${secondary}m\w\e[0m\g # '
 *     vim:                      # vim 模式（未来）
 *       ps1: '[NORMAL] \w\$ '
 *
 *   script: |                   # 内嵌 shell 脚本（主题加载时执行）
 *     export THEME_LOADED=1
 *     alias :status='echo theme: $MSH_THEME'
 *
 * === 状态选择逻辑 ===
 *
 *   1. 如果 geteuid()==0 → root 状态
 *   2. 如果上条命令失败 ($? != 0) → error 状态
 *   3. 否则 → default 状态
 *   4. 如果选定状态不存在，回退到 default
 *
 * === 颜色变量替换 ===
 *
 *   YAML 中的 ${primary} 会被替换为 colors.primary 的值。
 *   例如 \e[${primary}m → \e[1;32m
 *   这样修改一处颜色即可改变整个主题。
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "meuos/config.h"
#include "msh/msh.h"
#include "msh/i18n.h"
#include "msh/parse.h"
#include "msh/plugin.h"

/* === 颜色变量替换 === */
/* 将 ${var} 替换为 colors 表中的值。
 * 返回 malloc 的新字符串。 */
static char *substitute_colors(const char *input, const cfg_value_t *colors) {
    if (!input) return NULL;
    if (!colors || colors->type != CFG_TABLE) return strdup(input);

    size_t cap = strlen(input) * 2 + 64;
    char *out = malloc(cap);
    size_t outlen = 0;
    const char *p = input;

    while (*p) {
        if (p[0] == '$' && p[1] == '{') {
            /* 查找闭合 } */
            const char *end = strchr(p + 2, '}');
            if (end) {
                /* 提取变量名 */
                size_t vlen = end - p - 2;
                char varname[64];
                if (vlen >= sizeof(varname)) vlen = sizeof(varname) - 1;
                memcpy(varname, p + 2, vlen);
                varname[vlen] = '\0';

                /* 查找颜色值 */
                const cfg_value_t *val = cfg_get(colors, varname);
                const char *replacement = cfg_string(val, "");
                size_t rlen = strlen(replacement);

                while (outlen + rlen + 1 >= cap) { cap *= 2; out = realloc(out, cap); }
                memcpy(out + outlen, replacement, rlen);
                outlen += rlen;
                p = end + 1;
                continue;
            }
        }
        if (outlen + 2 >= cap) { cap *= 2; out = realloc(out, cap); }
        out[outlen++] = *p++;
    }
    out[outlen] = '\0';
    return out;
}

/* === 状态选择 === */
static const char *select_state(const cfg_value_t *states) {
    if (!states || states->type != CFG_TABLE) return "default";

    /* 1. root 用户 */
    if (geteuid() == 0) {
        const cfg_value_t *root = cfg_get(states, "root");
        if (root) return "root";
    }

    /* 2. 上条命令失败 */
    extern int msh_last_status;
    if (msh_last_status != 0) {
        const cfg_value_t *err = cfg_get(states, "error");
        if (err) return "error";
    }

    /* 3. 默认 */
    return "default";
}

/* === 应用 YAML 主题 === */
int msh_theme_apply_yaml(const cfg_value_t *cfg, const char *name) {
    if (!cfg || cfg->type != CFG_TABLE) return -1;

    /* 加载依赖插件 */
    const cfg_value_t *requires = cfg_get(cfg, "requires");
    if (requires && requires->type == CFG_LIST) {
        for (size_t i = 0; i < requires->u.list.count; i++) {
            const char *pname = requires->u.list.items[i];
            msh_plugin_load(pname);
        }
    }

    /* 获取颜色定义 */
    const cfg_value_t *colors = cfg_get(cfg, "colors");

    /* 获取状态表 */
    const cfg_value_t *states = cfg_get(cfg, "states");

    if (states && states->type == CFG_TABLE) {
        /* 有状态机：选择当前状态 */
        const char *state_name = select_state(states);
        const cfg_value_t *state = cfg_get(states, state_name);
        if (!state) state = cfg_get(states, "default");
        if (state && state->type == CFG_TABLE) {
            const cfg_value_t *ps1 = cfg_get(state, "ps1");
            const cfg_value_t *ps2 = cfg_get(state, "ps2");
            if (ps1) {
                const char *raw = cfg_string(ps1, "");
                char *expanded = substitute_colors(raw, colors);
                if (expanded) {
                    /* 转换 \e → 实际 ESC，\w → \w（保留给 prompt_expand 处理） */
                    /* MSH_PS1 需要保留 \e \w 等转义给 msh_prompt_expand */
                    setenv("MSH_PS1", expanded, 1);
                    free(expanded);
                }
            }
            if (ps2) {
                const char *raw = cfg_string(ps2, "");
                char *expanded = substitute_colors(raw, colors);
                if (expanded) {
                    setenv("MSH_PS2", expanded, 1);
                    free(expanded);
                }
            }
        }
    } else {
        /* 无状态机：直接读 ps1/ps2 字段 */
        const cfg_value_t *ps1 = cfg_get(cfg, "ps1");
        const cfg_value_t *ps2 = cfg_get(cfg, "ps2");
        if (ps1) {
            const char *raw = cfg_string(ps1, "");
            char *expanded = substitute_colors(raw, colors);
            if (expanded) {
                setenv("MSH_PS1", expanded, 1);
                free(expanded);
            }
        }
        if (ps2) {
            const char *raw = cfg_string(ps2, "");
            char *expanded = substitute_colors(raw, colors);
            if (expanded) {
                setenv("MSH_PS2", expanded, 1);
                free(expanded);
            }
        }
    }

    /* 执行内嵌脚本
     * 支持两种格式：
     *   1. 列表格式（推荐）：
     *     script:
     *       - export X=1
     *       - alias ll='ls -l'
     *   2. 字符串格式：
     *     script: 'export X=1; alias ll=ls -l'
     *
     * 注意：列表项中避免出现 "key: value" 模式（含冒号+空格），
     * 因为 YAML 解析器会将其误判为键值对。
     * 含冒号的命令请用字符串格式。
     */
    const cfg_value_t *script = cfg_get(cfg, "script");
    if (script) {
        if (script->type == CFG_LIST) {
            for (size_t i = 0; i < script->u.list.count; i++) {
                const char *line = script->u.list.items[i];
                if (!line) continue;
                /* 容错：YAML 解析器可能将含 : 的列表项存为
                 * cfg_value_t* 而非字符串。检测首字节是否可打印。 */
                unsigned char c = (unsigned char)line[0];
                if (c < 0x20 || c == 0x7f) continue;
                if (*line) msh_run_string(line, strlen(line));
            }
        } else {
            const char *code = cfg_string(script, "");
            if (*code) msh_run_string(code, strlen(code));
        }
    }

    setenv("MSH_THEME", name, 1);
    return 0;
}

/* === 内置 YAML 主题 === */

static const char theme_modern_yaml[] =
    "name: modern\n"
    "desc: 简约现代风格：彩色路径 + git 状态 + exit code\n"
    "colorscheme: dark\n"
    "requires:\n"
    "  - git\n"
    "colors:\n"
    "  primary: \"1;32\"\n"
    "  path: \"34\"\n"
    "  error: \"1;31\"\n"
    "  muted: \"90\"\n"
    "  accent: \"36\"\n"
    "states:\n"
    "  default:\n"
    "    ps1: '\\e[${primary}m\xe2\x9e\x9c\\e[0m \\e[${path}m\\w\\e[0m\\g \\p\\e[${primary}m\\$\\e[0m '\n"
    "    ps2: '\\e[${muted}m> \\e[0m'\n"
    "  error:\n"
    "    ps1: '\\e[${error}m\xe2\x9e\x9c\\e[0m \\e[${path}m\\w\\e[0m\\g \\p\\e[${error}m\\$\\e[0m '\n"
    "  root:\n"
    "    ps1: '\\e[${error}m\xe2\x9e\x9c\\e[0m \\e[${path}m\\w\\e[0m\\g \\p\\e[${error}m#\\e[0m '\n"
    "script:\n"
    "  - export THEME_FEATURES=git\n";

static const char theme_minimal_yaml[] =
    "name: minimal\n"
    "desc: 极简风格：纯文本\n"
    "colorscheme: none\n"
    "states:\n"
    "  default:\n"
    "    ps1: '$ '\n"
    "    ps2: '> '\n";

static const char theme_colorful_yaml[] =
    "name: colorful\n"
    "desc: 鲜艳风格：多色提示符\n"
    "colorscheme: dark\n"
    "colors:\n"
    "  user: \"1;32\"\n"
    "  path: \"1;34\"\n"
    "  prompt: \"1;33\"\n"
    "  muted: \"1;30\"\n"
    "states:\n"
    "  default:\n"
    "    ps1: '\\e[${user}m\\u@\\h\\e[0m:\\e[${path}m\\w\\e[0m\\g\\e[${prompt}m\\$\\e[0m '\n"
    "    ps2: '\\e[${muted}m> \\e[0m'\n";

static const char theme_powerline_yaml[] =
    "name: powerline\n"
    "desc: Powerline 风格：箭头分隔符\n"
    "colorscheme: dark\n"
    "requires:\n"
    "  - git\n"
    "colors:\n"
    "  bg1: \"44\"\n"
    "  bg2: \"41\"\n"
    "  fg: \"1;37\"\n"
    "  path: \"34\"\n"
    "states:\n"
    "  default:\n"
    "    ps1: '\\e[${fg};${bg1}m \\w \\e[0;${bg1}m\\e[${fg};${bg2}m\\g \\e[0;${bg2}m\\e[${fg};${bg2}m$ \\e[0m '\n"
    "    ps2: '\\e[${fg};${bg1}m> \\e[0m'\n";

static const char theme_clean_yaml[] =
    "name: clean\n"
    "desc: 干净清爽风格：仅路径和提示符\n"
    "colorscheme: light\n"
    "states:\n"
    "  default:\n"
    "    ps1: '\\w \\g\\$ '\n"
    "    ps2: '  '\n";

static const char theme_rainbow_yaml[] =
    "name: rainbow\n"
    "desc: 彩虹风格：每段不同颜色\n"
    "colorscheme: dark\n"
    "colors:\n"
    "  arrow: \"38;5;208\"\n"
    "  path: \"38;5;39\"\n"
    "  git: \"38;5;114\"\n"
    "  prompt: \"38;5;208\"\n"
    "  muted: \"38;5;240\"\n"
    "states:\n"
    "  default:\n"
    "    ps1: '\\e[${arrow}m\xe2\x9d\xaf\\e[${path}m \\w \\e[${git}m\\g\\e[${prompt}m\\$\\e[0m '\n"
    "    ps2: '\\e[${muted}m>\\e[0m '\n";

/* 内置 YAML 主题表 */
typedef struct {
    const char *name;
    const char *yaml;
} builtin_yaml_theme_t;

static const builtin_yaml_theme_t yaml_themes[] = {
    { "modern",   theme_modern_yaml },
    { "minimal",  theme_minimal_yaml },
    { "colorful", theme_colorful_yaml },
    { "powerline", theme_powerline_yaml },
    { "clean",    theme_clean_yaml },
    { "rainbow",  theme_rainbow_yaml },
    { NULL, NULL },
};

/* 检查是否为内置 YAML 主题，是则应用 */
int msh_theme_try_yaml_builtin(const char *name) {
    for (int i = 0; yaml_themes[i].name; i++) {
        if (strcmp(yaml_themes[i].name, name) == 0) {
            cfg_value_t *cfg = cfg_parse(yaml_themes[i].yaml,
                                         strlen(yaml_themes[i].yaml));
            if (cfg) {
                int rc = msh_theme_apply_yaml(cfg, name);
                cfg_value_free(cfg);
                return rc;
            }
            return -1;
        }
    }
    return -1; /* 不是内置 YAML 主题 */
}

/* 从文件加载 YAML 主题 */
int msh_theme_apply_yaml_file(const char *path, const char *name) {
    cfg_value_t *cfg = cfg_load_file(path);
    if (!cfg) return -1;
    int rc = msh_theme_apply_yaml(cfg, name);
    cfg_value_free(cfg);
    return rc;
}

/* 获取内置 YAML 主题列表（用于 :themes 列表） */
const builtin_yaml_theme_t *msh_yaml_themes(void) {
    return yaml_themes;
}
