/* msh/exec/compat.c — bash/zsh 兼容转换层
 *
 * 设计理念：
 *   msh 有自己的原生插件/主题系统。bash/zsh 兼容通过转换层实现：
 *   识别 bash/zsh 格式的配置，提取有价值的部分（别名、函数、环境变量），
 *   转换为 msh 原生格式后加载。
 *
 *   不尝试 100% 兼容 bash/zsh 语法——只提取常见模式。
 *   复杂的 bash/zsh 脚本应手动迁移为 .msh 插件。
 *
 * 支持的转换：
 *   1. .bashrc / .bash_profile → 提取 alias/export/PATH
 *   2. .zshrc → 提取 alias/export/PROMPT
 *   3. oh-my-zsh 主题名 → 映射到最接近的 msh 内置主题
 *   4. bash PS1 → msh PS1（转义序列转换）
 *
 * 用法：
 *   msh plugin compat bash    # 从 ~/.bashrc 导入
 *   msh plugin compat zsh     # 从 ~/.zshrc 导入
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>

#include "msh/msh.h"
#include "msh/i18n.h"
#include "msh/parse.h"

/* === 辅助函数 === */

/* 去除行首尾空白 */
static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

/* 去除行尾注释（# 开头的不算，只处理行内 # 注释）
 * 注意：不处理引号内的 #，简化处理 */
static void strip_inline_comment(char *line) {
    int in_squote = 0, in_dquote = 0;
    for (char *p = line; *p; p++) {
        if (*p == '\'' && !in_dquote) in_squote = !in_squote;
        else if (*p == '"' && !in_squote) in_dquote = !in_dquote;
        else if (*p == '#' && !in_squote && !in_dquote) {
            /* 行内注释：前面是空白 */
            if (p == line || isspace((unsigned char)p[-1])) {
                *p = '\0';
                return;
            }
        }
    }
}

/* === bash PS1 → msh PS1 转换 ===
 *
 * bash 转义 → msh 转义映射：
 *   \u  → \u   (用户名)
 *   \h  → \h   (主机名)
 *   \w  → \w   (工作目录)
 *   \W  → \W   (目录名)
 *   \$  → \$   (提示符)
 *   \n  → \n   (换行)
 *   \t  → \t   (时间)
 *   \[  → \[   (不可见开始)
 *   \]  → \]   (不可见结束)
 *   \e  → \e   (ESC)
 *   \s  → \s   (shell名)
 *   \v  → \v   (版本)
 *   \H  → \H   (完整主机名)
 *   \T  → \T   (12h时间)
 *
 * bash 和 msh 的 PS1 转义序列基本兼容，直接传递即可。
 * 需要处理的差异：
 *   - bash 的 \j (job count) → 省略
 *   - bash 的 \l (tty) → 省略
 *   - bash 的 \d (日期) → 省略
 *   - bash 的 \D{format} → 省略
 *   - bash 的 \[ \] 在 msh 中也支持
 */
static char *convert_bash_ps1(const char *bash_ps1) {
    if (!bash_ps1) return NULL;
    size_t len = strlen(bash_ps1);
    char *out = malloc(len * 2 + 1);
    size_t oi = 0;

    for (size_t i = 0; i < len; i++) {
        if (bash_ps1[i] == '\\' && i + 1 < len) {
            char c = bash_ps1[i + 1];
            switch (c) {
            /* 直接兼容的转义 */
            case 'u': case 'h': case 'w': case 'W': case '$':
            case 'n': case 't': case 'T': case 'H': case 's':
            case 'v': case 'e': case '[': case ']': case '\\':
                out[oi++] = '\\';
                out[oi++] = c;
                i++;
                break;
            /* 不支持的转义：跳过 */
            case 'j': case 'l': case 'd': case 'D': case 'r': case 'A':
                i++;
                /* \D{...} 跳过花括号内容 */
                if (c == 'D' && i + 1 < len && bash_ps1[i + 1] == '{') {
                    i++;
                    while (i < len && bash_ps1[i] != '}') i++;
                }
                break;
            default:
                /* 未知转义：保留原样 */
                out[oi++] = '\\';
                out[oi++] = c;
                i++;
                break;
            }
        } else {
            out[oi++] = bash_ps1[i];
        }
    }
    out[oi] = '\0';
    return out;
}

/* === 从文件提取 alias/export === */

typedef struct {
    int alias_count;
    int export_count;
    int other_count;
    int total;
} compat_stats_t;

static int process_rc_line(const char *line, compat_stats_t *stats) {
    char buf[4096];
    snprintf(buf, sizeof(buf), "%s", line);
    strip_inline_comment(buf);
    char *p = trim(buf);
    if (!*p) return 0;

    stats->total++;

    /* alias name='value' 或 alias name="value" 或 alias name=value */
    if (strncmp(p, "alias ", 6) == 0) {
        /* msh 兼容 alias 语法，直接执行 */
        int rc = msh_run_string(p, strlen(p));
        if (rc == 0) stats->alias_count++;
        return 0;
    }

    /* export VAR=value 或 export VAR */
    if (strncmp(p, "export ", 7) == 0) {
        int rc = msh_run_string(p, strlen(p));
        if (rc == 0) stats->export_count++;
        return 0;
    }

    /* PS1=... → 转换为 msh PS1（必须在通用 VAR= 之前检测） */
    if (strncmp(p, "PS1=", 4) == 0) {
        /* 提取 PS1 值 */
        char *val = p + 4;
        /* 去掉引号 */
        char ps1_val[2048] = {0};
        if (*val == '\'') {
            size_t l = strlen(val + 1);
            if (l > 0 && val[l] == '\'') l--;
            snprintf(ps1_val, sizeof(ps1_val), "%.*s", (int)l, val + 1);
        } else if (*val == '"') {
            size_t l = strlen(val + 1);
            if (l > 0 && val[l] == '"') l--;
            snprintf(ps1_val, sizeof(ps1_val), "%.*s", (int)l, val + 1);
        } else {
            snprintf(ps1_val, sizeof(ps1_val), "%s", val);
        }
        char *converted = convert_bash_ps1(ps1_val);
        if (converted) {
            char cmd[4096];
            snprintf(cmd, sizeof(cmd), "export MSH_PS1='%s'", converted);
            msh_run_string(cmd, strlen(cmd));
            free(converted);
            stats->export_count++;
        }
        return 0;
    }

    /* VAR=value (无 export) */
    if (strchr(p, '=') && !isspace((unsigned char)*p)) {
        /* 检查是否是简单的 VAR=value */
        char *eq = strchr(p, '=');
        if (eq && eq > p) {
            /* 检查变量名是否合法 */
            int valid = 1;
            for (char *c = p; c < eq; c++) {
                if (!(isalnum((unsigned char)*c) || *c == '_')) { valid = 0; break; }
            }
            if (valid) {
                int rc = msh_run_string(p, strlen(p));
                if (rc == 0) stats->export_count++;
                return 0;
            }
        }
    }

    /* PATH=... 通常是 PATH 追加 */
    if (strncmp(p, "PATH=", 5) == 0 || strncmp(p, "export PATH", 11) == 0) {
        int rc = msh_run_string(p, strlen(p));
        if (rc == 0) stats->export_count++;
        return 0;
    }

    /* source / . 命令：跳过（避免副作用） */
    if (strncmp(p, "source ", 7) == 0 || strncmp(p, ". ", 2) == 0) {
        return 0;
    }

    /* 其他行：跳过（不执行未知命令，安全第一） */
    stats->other_count++;
    return 0;
}

/* 从 rc 文件提取配置 */
static int import_rc_file(const char *rcpath, compat_stats_t *stats) {
    FILE *f = fopen(rcpath, "r");
    if (!f) return -1;

    char line[4096];
    char multiline[8192] = {0};
    int in_multiline = 0;

    while (fgets(line, sizeof(line), f)) {
        /* 去掉换行 */
        size_t l = strlen(line);
        while (l && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';

        /* 处理多行续行（行尾 \） */
        if (l > 0 && line[l-1] == '\\') {
            line[l-1] = '\0';
            if (in_multiline) {
                strncat(multiline, line, sizeof(multiline) - strlen(multiline) - 1);
            } else {
                snprintf(multiline, sizeof(multiline), "%s", line);
                in_multiline = 1;
            }
            continue;
        }
        if (in_multiline) {
            strncat(multiline, line, sizeof(multiline) - strlen(multiline) - 1);
            process_rc_line(multiline, stats);
            multiline[0] = '\0';
            in_multiline = 0;
        } else {
            process_rc_line(line, stats);
        }
    }

    fclose(f);
    return 0;
}

/* === oh-my-zsh 主题映射 === */
typedef struct {
    const char *zsh_theme;
    const char *msh_theme;
} theme_mapping_t;

static const theme_mapping_t theme_map[] = {
    { "robbyrussell",  "modern" },     /* 默认 oh-my-zsh 主题 → modern */
    { "agnoster",      "powerline" },
    { "powerlevel10k", "powerline" },
    { "powerlevel9k",  "powerline" },
    { "af-magic",      "colorful" },
    { "bira",          "colorful" },
    { "bobby",         "colorful" },
    { "clean",         "clean" },
    { "crunch",        "minimal" },
    { "dst",           "minimal" },
    { "fishy",         "clean" },
    { "gallifrey",     "colorful" },
    { "gallois",       "colorful" },
    { "garyblessington","minimal" },
    { "gentoo",        "minimal" },
    { "geoffgarside",  "minimal" },
    { "halfeld",       "minimal" },
    { "jaischeema",    "colorful" },
    { "jbergantine",   "colorful" },
    { "jispwoso",      "minimal" },
    { "jkallen",       "minimal" },
    { "juanghurtado",  "colorful" },
    { "kolo",          "colorful" },
    { "kphoen",        "colorful" },
    { "lambda",        "minimal" },
    { "maran",         "colorful" },
    { "mh",            "minimal" },
    { "michelebologna","minimal" },
    { "miloshadzic",   "colorful" },
    { "minimal",       "minimal" },
    { "mortalscumbag", "colorful" },
    { "mrtazz",        "minimal" },
    { "peepcode",      "clean" },
    { "refined",       "clean" },
    { "risto",         "minimal" },
    { "rkj-repos",     "minimal" },
    { "sammy",         "colorful" },
    { "simple",        "minimal" },
    { "smt",           "minimal" },
    { "soliah",        "colorful" },
    { "sonicradish",   "colorful" },
    { "sorin",         "colorful" },
    { "steeef",        "colorful" },
    { "sunaku",        "minimal" },
    { "terminalparty", "minimal" },
    { "theunraveler",  "minimal" },
    { "tjkirch",       "minimal" },
    { "trapd00r",      "colorful" },
    { "wedisagree",    "colorful" },
    { "wezm",          "minimal" },
    { "wuff",          "minimal" },
    { "xiong-chiamiov","colorful" },
    { "ys",            "clean" },
    { "zhann",         "colorful" },
    { NULL, NULL }
};

static const char *map_zsh_theme(const char *zsh_name) {
    for (int i = 0; theme_map[i].zsh_theme; i++) {
        if (strcmp(theme_map[i].zsh_theme, zsh_name) == 0)
            return theme_map[i].msh_theme;
    }
    /* 未知主题 → modern（安全默认） */
    return "modern";
}

/* === 公开接口 === */

/* 导入 bash/zsh 配置 */
int msh_compat_import(const char *target) {
    const char *home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "msh: HOME not set\n");
        return 1;
    }

    compat_stats_t stats = {0};
    char rcpath[PATH_MAX];

    if (strcmp(target, "bash") == 0) {
        /* 尝试 ~/.bashrc → ~/.bash_profile → ~/.profile */
        const char *rc_files[] = {".bashrc", ".bash_profile", ".profile", NULL};
        int found = 0;
        for (int i = 0; rc_files[i]; i++) {
            snprintf(rcpath, sizeof(rcpath), "%s/%s", home, rc_files[i]);
            if (access(rcpath, R_OK) == 0) {
                printf("导入 bash 配置: %s\n", rcpath);
                import_rc_file(rcpath, &stats);
                found = 1;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "msh: 未找到 bash 配置文件\n");
            return 1;
        }
    } else if (strcmp(target, "zsh") == 0) {
        snprintf(rcpath, sizeof(rcpath), "%s/.zshrc", home);
        if (access(rcpath, R_OK) != 0) {
            fprintf(stderr, "msh: 未找到 ~/.zshrc\n");
            return 1;
        }
        printf("导入 zsh 配置: %s\n", rcpath);
        import_rc_file(rcpath, &stats);

        /* 检测 oh-my-zsh 主题 */
        const char *zsh_theme = getenv("ZSH_THEME");
        if (!zsh_theme) {
            /* 从 .zshrc 提取 ZSH_THEME= */
            FILE *f = fopen(rcpath, "r");
            if (f) {
                char line[4096];
                while (fgets(line, sizeof(line), f)) {
                    char *p = trim(line);
                    if (strncmp(p, "ZSH_THEME=", 10) == 0) {
                        char *val = p + 10;
                        /* 去引号 */
                        if (*val == '"') val++;
                        char *end = val + strlen(val) - 1;
                        while (end > val && (*end == '"' || *end == '\'' || *end == '\n'))
                            *end-- = '\0';
                        zsh_theme = strdup(val);
                        break;
                    }
                }
                fclose(f);
            }
        }

        if (zsh_theme && *zsh_theme && strcmp(zsh_theme, "random") != 0) {
            const char *msh_theme = map_zsh_theme(zsh_theme);
            printf("oh-my-zsh 主题 '%s' → msh 主题 '%s'\n", zsh_theme, msh_theme);
            extern int msh_theme_apply(const char *name);
            msh_theme_apply(msh_theme);
        }
    } else {
        fprintf(stderr, "msh: compat: unsupported target '%s' (use: bash|zsh)\n", target);
        return 1;
    }

    printf("导入完成: %d 别名, %d 环境变量, %d 行已跳过\n",
           stats.alias_count, stats.export_count, stats.other_count);
    return 0;
}
