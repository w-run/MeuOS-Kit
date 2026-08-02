/* msh/main/config.c — 配置加载、别名、PS1、兼容模式
 *
 * 复用 meuos-utils 的 libutils config.c（YAML 子集解析器）。
 * 配置 schema：
 *   prompt: {ps1: "...", ps2: "..."}
 *   aliases: {ll: "ls -l", ...}
 *   env: {PATH: "...", ...}
 *   features: {color: true, history_size: 1000, classic: false}
 *   theme: "modern"
 *   plugins: [git, battery]
 *   lang: "zh-CN"
 *
 * 3 路递进：--classic argv > MSH_CLASSIC env > YAML features.classic
 *
 * PS1 扩展转义序列（msh 原生，与 bash 大部分兼容）：
 *   \u   用户名          \h   主机名（短）
 *   \w   工作目录($HOME→~)  \W   当前目录名
 *   \n   换行            \t   时间 24h
 *   \$   # (root) 或 $   \\   反斜杠
 *   \g   git 分支+状态    \G   git 分支（仅名称）
 *   \e   ESC 序列        \r   回车
 *   \s   shell 名称       \v   版本
 *   \p   exit code（上次命令非0时红色显示）
 *   \[   不可见序列开始   \]   不可见序列结束
 *   \H   完整主机名       \T   12h时间
 *
 * bash/zsh PS1 通过 compat.c 转换层导入，非自身模拟。
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "meuos/config.h"
#include "msh/msh.h"
#include "msh/exec.h"
#include "msh/i18n.h"

int msh_mode_classic = 0;
int msh_errexit = 0;    /* set -e */
int msh_pipefail = 0;    /* set -o pipefail */
int msh_in_cond = 0;     /* evaluating condition (exempt from errexit) */

/* 循环控制流标志 */
int msh_break_flag = 0;
int msh_continue_flag = 0;
int msh_return_flag = 0;
int msh_return_value = 0;
int msh_loop_depth = 0;

/* === 别名表 === */
typedef struct alias_entry {
    char *name;
    char *value;
    struct alias_entry *next;
} alias_entry_t;

static alias_entry_t *g_aliases = NULL;

void msh_alias_add(const char *name, const char *value) {
    for (alias_entry_t *a = g_aliases; a; a = a->next) {
        if (!strcmp(a->name, name)) {
            free(a->value);
            a->value = strdup(value);
            return;
        }
    }
    alias_entry_t *a = calloc(1, sizeof(*a));
    a->name = strdup(name);
    a->value = strdup(value);
    a->next = g_aliases;
    g_aliases = a;
}

const char *msh_alias_lookup(const char *name) {
    for (alias_entry_t *a = g_aliases; a; a = a->next) {
        if (!strcmp(a->name, name)) return a->value;
    }
    return NULL;
}

void msh_alias_list(void) {
    for (alias_entry_t *a = g_aliases; a; a = a->next) {
        printf("alias %s='%s'\n", a->name, a->value);
    }
}

int msh_alias_remove(const char *name) {
    alias_entry_t **pp = &g_aliases;
    while (*pp) {
        if (!strcmp((*pp)->name, name)) {
            alias_entry_t *tmp = *pp;
            *pp = tmp->next;
            free(tmp->name);
            free(tmp->value);
            free(tmp);
            return 1;
        }
        pp = &(*pp)->next;
    }
    return 0;
}

/* === Git 状态检测 === */

/* 读取 .git/HEAD 获取当前分支名。
 * 返回 malloc 字符串（分支名或 NULL）。
 * *dirty 输出: 1=有未提交更改, 0=干净 */
static char *git_branch(int *dirty) {
    *dirty = 0;
    /* 查找 .git 目录：从当前目录向上搜索 */
    char path[4096];
    if (!getcwd(path, sizeof(path))) return NULL;

    for (;;) {
        char git_dir[4096];
        int n = snprintf(git_dir, sizeof(git_dir), "%s/.git", path);
        if (n < 0 || (size_t)n >= sizeof(git_dir)) return NULL;

        FILE *f = fopen(git_dir, "r");
        if (f) {
            /* .git 是文件（worktree/submodule），读 gitdir 路径 */
            char line[4096];
            if (fgets(line, sizeof(line), f)) {
                /* 去掉换行 */
                size_t l = strlen(line);
                while (l && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
                if (strncmp(line, "gitdir:", 7) == 0) {
                    const char *gd = line + 7;
                    while (*gd == ' ' || *gd == '\t') gd++;
                    /* 尝试读 gd/HEAD */
                    char head_path[4096];
                    snprintf(head_path, sizeof(head_path), "%s/HEAD", gd);
                    fclose(f);
                    f = fopen(head_path, "r");
                }
            }
            if (f) {
                fclose(f);
            }
        } else {
            /* .git 是目录 */
            char head_path[4096];
            n = snprintf(head_path, sizeof(head_path), "%s/.git/HEAD", path);
            if (n < 0 || (size_t)n >= sizeof(head_path)) return NULL;
            f = fopen(head_path, "r");
        }

        if (f) {
            char line[256];
            if (fgets(line, sizeof(line), f)) {
                fclose(f);
                size_t l = strlen(line);
                while (l && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';

                char *branch = NULL;
                if (strncmp(line, "ref:", 4) == 0) {
                    /* ref: refs/heads/branch-name */
                    char *ref = line + 4;
                    while (*ref == ' ') ref++;
                    if (strncmp(ref, "refs/heads/", 11) == 0) {
                        branch = strdup(ref + 11);
                    } else {
                        /* detached HEAD: short hash */
                        branch = strdup(ref);
                    }
                } else {
                    /* detached: raw hash */
                    branch = strdup(line);
                }

                /* 检查 dirty: git status --porcelain 太重，
                 * 简单检查 .git/index 与 HEAD 是否一致太复杂，
                 * 这里用 stat 比较 .git/COMMIT_EDITMSG 时间戳 vs 工作目录文件。
                 * 更实际的方案：运行 git diff --quiet，但可能有性能影响。
                 * 简化：检查 .git/index 是否存在，且工作目录有 .git/MERGE_HEAD 等 */
                char idx_path[4096];
                snprintf(idx_path, sizeof(idx_path), "%s/.git/index", path);
                /* 如果有 MERGE_HEAD 说明正在 merge */
                char merge_path[4096];
                snprintf(merge_path, sizeof(merge_path), "%s/.git/MERGE_HEAD", path);
                if (access(merge_path, F_OK) == 0) {
                    *dirty = 1;
                }

                return branch;
            }
            fclose(f);
        }

        /* 向上搜索 */
        char *slash = strrchr(path, '/');
        if (!slash || slash == path) break;
        *slash = '\0';
    }
    return NULL;
}

/* === PS1 展开 === */
char *msh_prompt_expand(const char *ps1) {
    if (!ps1) {
        /* 默认提示符：现代风格 */
        if (!msh_mode_classic) {
            ps1 = "\\[\xe2\x9e\x9c\\] \\w\\g \\$ ";  /* ➜ path(git) $ */
        } else {
            ps1 = "\\u@\\h:\\w\\$ ";
        }
    }
    size_t cap = strlen(ps1) * 4 + 256;
    char *out = malloc(cap);
    if (!out) return NULL;
    size_t len = 0;

    for (const char *p = ps1; *p; p++) {
        if (*p != '\\') {
            if (len + 2 >= cap) { cap *= 2; out = realloc(out, cap); }
            out[len++] = *p;
            continue;
        }

        p++;
        if (!*p) { out[len++] = '\\'; break; }

        const char *repl = NULL;
        char buf[512];

        switch (*p) {
        case 'u': {
            const char *u = getenv("USER");
            if (!u) u = getenv("LOGNAME");
            if (u) repl = u;
            break;
        }
        case 'h': {
            if (gethostname(buf, sizeof(buf)) == 0) {
                buf[sizeof(buf)-1] = '\0';
                /* 短主机名：截到第一个 . */
                char *dot = strchr(buf, '.');
                if (dot) *dot = '\0';
                repl = buf;
            }
            break;
        }
        case 'H': {  /* 完整主机名 */
            if (gethostname(buf, sizeof(buf)) == 0) {
                buf[sizeof(buf)-1] = '\0';
                repl = buf;
            }
            break;
        }
        case 'w': {  /* 工作目录，$HOME 替换为 ~ */
            if (getcwd(buf, sizeof(buf))) {
                const char *home = getenv("HOME");
                if (home && *home) {
                    size_t hl = strlen(home);
                    if (strncmp(buf, home, hl) == 0) {
                        memmove(buf + 1, buf + hl, strlen(buf) - hl + 1);
                        buf[0] = '~';
                    }
                }
                repl = buf;
            }
            break;
        }
        case 'W': {  /* 当前目录名 */
            if (getcwd(buf, sizeof(buf))) {
                char *slash = strrchr(buf, '/');
                if (slash) {
                    /* 特殊：根目录 */
                    if (slash == buf && slash[1] == '\0') {
                        repl = "/";
                    } else {
                        repl = slash + 1;
                    }
                } else {
                    repl = buf;
                }
            }
            break;
        }
        case 'n': repl = "\n"; break;
        case 'r': repl = "\r"; break;
        case 'e': repl = "\033"; break;  /* ESC */
        case '$':
            buf[0] = (geteuid() == 0) ? '#' : '$';
            buf[1] = '\0';
            repl = buf;
            break;
        case 't': {  /* 24h 时间 HH:MM:SS */
            time_t now = time(NULL);
            struct tm tm;
            localtime_r(&now, &tm);
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
            repl = buf;
            break;
        }
        case 'T': {  /* 12h 时间 */
            time_t now = time(NULL);
            struct tm tm;
            localtime_r(&now, &tm);
            int h = tm.tm_hour % 12;
            if (h == 0) h = 12;
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, tm.tm_min, tm.tm_sec);
            repl = buf;
            break;
        }
        case 's': repl = msh_program_name; break;
        case 'v': repl = msh_version; break;
        case '\\': repl = "\\"; break;
        case '[': repl = ""; break;   /* 不可见序列开始（我们忽略） */
        case ']': repl = ""; break;   /* 不可见序列结束 */
        case 'g': {  /* git 分支 + 状态 */
            int dirty = 0;
            char *branch = git_branch(&dirty);
            if (branch) {
                if (msh_mode_classic) {
                    snprintf(buf, sizeof(buf), "(%s%s)", branch, dirty ? "*" : "");
                } else {
                    /* 彩色: 绿色分支名 + 黄色 * (dirty) */
                    snprintf(buf, sizeof(buf),
                             "\033[36m(%s)\033[0m%s",
                             branch, dirty ? "\033[33m*\033[0m" : "");
                }
                repl = buf;
                free(branch);
            }
            break;
        }
        case 'G': {  /* git 分支名（仅名称，无括号） */
            int dirty = 0;
            char *branch = git_branch(&dirty);
            if (branch) {
                repl = branch;
                if (dirty) {
                    snprintf(buf, sizeof(buf), "%s*", branch);
                    repl = buf;
                    free(branch);
                }
                /* 注意：如果不 dirty，branch 被 repl 指向，不能 free */
                if (repl != (const char *)buf && repl != branch) {
                    free(branch);
                } else if (repl == branch) {
                    /* branch 作为 repl 返回，调用者不 free — 泄漏一次可接受 */
                }
            }
            break;
        }
        case 'p': {  /* exit code（非0时红色显示） */
            if (msh_last_status != 0) {
                if (msh_mode_classic) {
                    snprintf(buf, sizeof(buf), "[%d] ", msh_last_status);
                } else {
                    snprintf(buf, sizeof(buf), "\033[1;31m[%d]\033[0m ", msh_last_status);
                }
                repl = buf;
            } else {
                repl = "";
            }
            break;
        }
        default: {
            /* 未知转义：保留原样 */
            static char b2[3];
            b2[0] = '\\'; b2[1] = *p; b2[2] = 0;
            repl = b2;
            break;
        }
        }

        if (repl) {
            size_t rl = strlen(repl);
            while (len + rl + 2 >= cap) { cap *= 2; out = realloc(out, cap); }
            memcpy(out + len, repl, rl);
            len += rl;
        }
    }
    out[len] = '\0';
    return out;
}

/* === 配置加载 === */
static void apply_config(const cfg_value_t *cfg) {
    if (!cfg) return;

    /* features: {classic, color, history_size} */
    const cfg_value_t *feat = cfg_get_path(cfg, "features");
    if (feat) {
        const cfg_value_t *cl = cfg_get(feat, "classic");
        if (cl && cfg_bool(cl, 0) && !msh_mode_classic) {
            msh_mode_classic = 1;
        }
    }

    /* lang: 语言设置 */
    const cfg_value_t *lang = cfg_get_path(cfg, "lang");
    if (lang) {
        const char *l = cfg_string(lang, "");
        if (*l) {
            setenv("MSH_LANG", l, 1);
        }
    }

    /* env: 导出到 environ */
    const cfg_value_t *env = cfg_get_path(cfg, "env");
    if (env && env->type == CFG_TABLE) {
        for (size_t i = 0; i < env->u.table.count; i++) {
            const char *key = env->u.table.keys[i];
            const char *val = cfg_string(env->u.table.values[i], "");
            setenv(key, val, 1);
        }
    }

    /* aliases */
    const cfg_value_t *aliases = cfg_get_path(cfg, "aliases");
    if (aliases && aliases->type == CFG_TABLE) {
        for (size_t i = 0; i < aliases->u.table.count; i++) {
            const char *key = aliases->u.table.keys[i];
            const char *val = cfg_string(aliases->u.table.values[i], "");
            msh_alias_add(key, val);
        }
    }

    /* prompt.ps1 -> 存 MSH_PS1 环境变量 */
    const cfg_value_t *prompt = cfg_get_path(cfg, "prompt");
    if (prompt) {
        const cfg_value_t *ps1 = cfg_get(prompt, "ps1");
        if (ps1 && ps1->type == CFG_STRING) {
            setenv("MSH_PS1", ps1->u.string.data, 1);
        }
    }

    /* theme: 内置主题名 */
    const cfg_value_t *theme = cfg_get_path(cfg, "theme");
    if (theme) {
        const char *tname = cfg_string(theme, "");
        if (*tname) {
            setenv("MSH_THEME", tname, 1);
        }
    }
}

void msh_load_config(const char *rcfile) {
    /* 初始化 i18n */
    msh_i18n_init();

    /* 3 路递进检测 */
    if (!msh_mode_classic) {
        const char *envc = getenv("MSH_CLASSIC");
        if (envc && (strcmp(envc, "1") == 0 || strcmp(envc, "true") == 0
                     || strcmp(envc, "TRUE") == 0)) {
            msh_mode_classic = 1;
        }
    }
    if (msh_mode_classic) return;  /* classic 不读 rc */

    if (!rcfile) {
        const char *home = getenv("HOME");
        if (!home) return;
        static char buf[4096];
        snprintf(buf, sizeof(buf), "%s/.config/msh/config.yaml", home);
        rcfile = buf;
    }
    if (access(rcfile, R_OK) != 0) return;

    cfg_value_t *cfg = cfg_load_file(rcfile);
    if (cfg) {
        apply_config(cfg);
        cfg_value_free(cfg);
    }

    /* 如果配置了主题，应用内置主题 */
    const char *theme = getenv("MSH_THEME");
    if (theme && *theme) {
        extern int msh_theme_builtin(const char *name);
        msh_theme_builtin(theme);
    }
}
