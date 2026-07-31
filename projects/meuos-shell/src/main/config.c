/* msh/main/config.c — 配置加载、别名、PS1、兼容模式
 *
 * 复用 meuos-utils 的 libutils config.c（YAML 子集解析器）。
 * 配置 schema：
 *   prompt: {ps1: "...", ps2: "..."}
 *   aliases: {ll: "ls -l", ...}
 *   env: {PATH: "...", ...}
 *   features: {color: true, history_size: 1000, classic: false}
 *
 * 3 路递进：--classic argv > MSH_CLASSIC env > YAML features.classic
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

int msh_mode_classic = 0;

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

/* === PS1 展开 === */
char *msh_prompt_expand(const char *ps1) {
    if (!ps1) ps1 = "\\u@\\h:\\w\\$ ";
    size_t cap = strlen(ps1) * 2 + 64;
    char *out = malloc(cap);
    if (!out) return NULL;
    size_t len = 0;
    for (const char *p = ps1; *p; p++) {
        if (*p == '\\' && p[1]) {
            p++;
            const char *repl = NULL;
            char buf[64];
            switch (*p) {
            case 'u': { const char *u = getenv("USER"); if (u) repl = u; break; }
            case 'h': {
                if (gethostname(buf, sizeof(buf)) == 0) { buf[sizeof(buf)-1] = '\0'; repl = buf; }
                break;
            }
            case 'w': {
                if (getcwd(buf, sizeof(buf))) repl = buf;
                break;
            }
            case 'n': repl = "\n"; break;
            case '$':
                buf[0] = (geteuid() == 0) ? '#' : '$';
                buf[1] = '\0';
                repl = buf;
                break;
            case 't': {
                /* 24h 时间 */
                time_t now = time(NULL);
                struct tm tm;
                localtime_r(&now, &tm);
                snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
                repl = buf;
                break;
            }
            case '\\': repl = "\\"; break;
            default: {
                static char b2[3];
                b2[0] = '\\'; b2[1] = *p; b2[2] = 0;
                repl = b2;
                break;
            }
            }
            if (repl) {
                size_t rl = strlen(repl);
                if (len + rl + 1 >= cap) {
                    cap *= 2;
                    out = realloc(out, cap);
                }
                memcpy(out + len, repl, rl);
                len += rl;
            }
        } else {
            if (len + 2 >= cap) {
                cap *= 2;
                out = realloc(out, cap);
            }
            out[len++] = *p;
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
}

void msh_load_config(const char *rcfile) {
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
}
