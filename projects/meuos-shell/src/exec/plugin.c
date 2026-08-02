/* msh/exec/plugin.c — msh 原生插件/主题引擎
 *
 * 设计理念：
 *   msh 拥有自己的原生插件和主题系统，简洁、易开发。
 *   bash/zsh 兼容通过独立的转换层（compat.c）实现，而非自身模拟。
 *
 * === 插件格式 ===
 *   插件是 .msh 脚本，顶部用注释声明元数据：
 *
 *     # @plugin git
 *     # @desc Git 版本控制别名和补全
 *     # @version 1.0
 *     # @author msh
 *
 *     alias gst='git status'
 *     alias gd='git diff'
 *     complete -c git
 *
 *   插件可以使用任何 msh 内建命令：alias, export, complete, 函数定义等。
 *   无需学习特殊 API——就是写 shell 脚本。
 *
 * === 主题格式 ===
 *   主题是 .msh 脚本，设置 PS1/PS2 变量：
 *
 *     # @theme modern
 *     # @desc 简约现代风格
 *     # @colorscheme dark
 *
 *     export MSH_PS1='\[\e[1;32m\]➜\[\e[0m\] \[\e[34m\]\w\[\e[0m\]\g \$ '
 *     export MSH_PS2='> '
 *
 *   PS1 支持的扩展转义序列见 config.c msh_prompt_expand() 文档。
 *
 * === 文件位置 ===
 *   用户插件: ~/.msh/plugins/<name>.msh 或 ~/.msh/plugins/<name>/init.msh
 *   用户主题: ~/.msh/themes/<name>.msh
 *   内置插件/主题: 编译时嵌入，无需文件
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

#include "msh/msh.h"
#include "msh/i18n.h"
#include "msh/parse.h"
#include "msh/plugin.h"

#define MAX_PLUGINS 64
#define MAX_THEMES  32

/* === 插件/主题元数据 === */
typedef struct {
    char name[64];
    char desc[256];
    char version[32];
    char author[64];
} plugin_meta_t;

typedef struct {
    char name[64];
    char path[PATH_MAX];
    int enabled;
    int builtin;
    plugin_meta_t meta;
} plugin_entry_t;

typedef struct {
    char name[64];
    char path[PATH_MAX];
    int builtin;
    plugin_meta_t meta;
} theme_entry_t;

static plugin_entry_t plugins[MAX_PLUGINS];
static int num_plugins = 0;
static theme_entry_t themes[MAX_THEMES];
static int num_themes = 0;

/* === 路径 === */
static const char *get_plugin_dir(void) {
    static char path[PATH_MAX];
    const char *home = getenv("HOME");
    if (home) {
        snprintf(path, sizeof(path), "%s/.msh/plugins", home);
        return path;
    }
    return "/tmp/msh-plugins";
}

static const char *get_theme_dir(void) {
    static char path[PATH_MAX];
    const char *home = getenv("HOME");
    if (home) {
        snprintf(path, sizeof(path), "%s/.msh/themes", home);
        return path;
    }
    return "/tmp/msh-themes";
}

/* === 工具函数 === */
static int source_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return -1; }
    char *buf = malloc(sz + 1);
    size_t nread = fread(buf, 1, sz, f);
    fclose(f);
    buf[nread] = '\0';
    int rc = msh_run_string(buf, nread);
    free(buf);
    return rc;
}

/* 从 .msh 脚本内容解析元数据注释（# @plugin, # @desc 等） */
static void parse_meta(const char *code, plugin_meta_t *meta) {
    memset(meta, 0, sizeof(*meta));
    const char *p = code;
    while (p && *p) {
        /* 跳过空白 */
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '#') {
            /* 遇到非注释行，停止解析元数据 */
            if (*p == '\n') { p++; continue; }
            break;
        }
        p++; /* skip # */
        if (*p == ' ') p++; /* skip space after # */

        if (strncmp(p, "@plugin", 7) == 0) {
            p += 7; while (*p == ' ') p++;
            snprintf(meta->name, sizeof(meta->name), "%s", p);
            char *nl = strchr(meta->name, '\n'); if (nl) *nl = '\0';
        } else if (strncmp(p, "@theme", 6) == 0) {
            p += 6; while (*p == ' ') p++;
            snprintf(meta->name, sizeof(meta->name), "%s", p);
            char *nl = strchr(meta->name, '\n'); if (nl) *nl = '\0';
        } else if (strncmp(p, "@desc", 5) == 0) {
            p += 5; while (*p == ' ') p++;
            snprintf(meta->desc, sizeof(meta->desc), "%s", p);
            char *nl = strchr(meta->desc, '\n'); if (nl) *nl = '\0';
        } else if (strncmp(p, "@version", 8) == 0) {
            p += 8; while (*p == ' ') p++;
            snprintf(meta->version, sizeof(meta->version), "%s", p);
            char *nl = strchr(meta->version, '\n'); if (nl) *nl = '\0';
        } else if (strncmp(p, "@author", 7) == 0) {
            p += 7; while (*p == ' ') p++;
            snprintf(meta->author, sizeof(meta->author), "%s", p);
            char *nl = strchr(meta->author, '\n'); if (nl) *nl = '\0';
        }

        /* 跳到下一行 */
        p = strchr(p, '\n');
        if (p) p++;
    }
}

/* === 内置插件（编译时嵌入） ===
 *
 * 内置插件使用 .msh 脚本格式，与用户插件完全相同。
 * 这样用户可以参考内置插件学习开发自己的插件。
 */

static const char * const builtin_plugin_code[] = {
    /* git — Git 版本控制集成 */
    "# @plugin git\n"
    "# @desc Git 版本控制别名和补全\n"
    "# @version 1.0\n"
    "# @author msh\n"
    "\n"
    "alias gst='git status'\n"
    "alias gd='git diff'\n"
    "alias gl='git log --oneline -20'\n"
    "alias gp='git push'\n"
    "alias ga='git add'\n"
    "alias gc='git commit'\n"
    "alias gco='git checkout'\n"
    "alias gb='git branch'\n"
    "alias gm='git merge'\n"
    "alias gr='git rebase'\n"
    "alias gpl='git pull'\n"
    "alias gcl='git clone'\n"
    "alias gca='git commit --amend'\n"
    "alias gpr='git pull --rebase'\n"
    "alias gst='git status -s'\n"
    "alias glg='git log --graph --oneline --all'\n"
    "complete -c git\n",

    /* exit-code — 命令失败时显示退出码 */
    "# @plugin exit-code\n"
    "# @desc 命令失败时显示退出码\n"
    "# @version 1.0\n"
    "# @author msh\n"
    "\n"
    "alias _check='[ $? -ne 0 ] && echo \"exit: $?\"'\n",

    /* virtualenv — Python 虚拟环境提示 */
    "# @plugin virtualenv\n"
    "# @desc Python 虚拟环境管理别名\n"
    "# @version 1.0\n"
    "# @author msh\n"
    "\n"
    "alias venv='source venv/bin/activate'\n"
    "alias deact='deactivate'\n"
    "alias mkvenv='python3 -m venv venv && source venv/bin/activate'\n",

    /* docker — Docker 容器管理别名 */
    "# @plugin docker\n"
    "# @desc Docker 容器管理别名\n"
    "# @version 1.0\n"
    "# @author msh\n"
    "\n"
    "alias dk='docker'\n"
    "alias dkps='docker ps'\n"
    "alias dkpsa='docker ps -a'\n"
    "alias dki='docker images'\n"
    "alias dkrm='docker rm'\n"
    "alias dkrmi='docker rmi'\n"
    "alias dkrun='docker run --rm -it'\n"
    "alias dkexec='docker exec -it'\n"
    "alias dkbuild='docker build'\n"
    "alias dkcomp='docker compose'\n"
    "complete -c docker\n",

    /* extract — 通用归档解压 */
    "# @plugin extract\n"
    "# @desc 通用归档解压函数\n"
    "# @version 1.0\n"
    "# @author msh\n"
    "\n"
    "extract() {\n"
    "  case \"$1\" in\n"
    "    *.tar.gz|*.tgz) tar xzf \"$1\" ;;\n"
    "    *.tar.bz2|*.tbz) tar xjf \"$1\" ;;\n"
    "    *.tar.xz|*.txz) tar xJf \"$1\" ;;\n"
    "    *.tar) tar xf \"$1\" ;;\n"
    "    *.gz) gunzip \"$1\" ;;\n"
    "    *.bz2) bunzip2 \"$1\" ;;\n"
    "    *.xz) unxz \"$1\" ;;\n"
    "    *.zip) unzip \"$1\" ;;\n"
    "    *.7z) 7z x \"$1\" ;;\n"
    "    *) echo \"extract: unknown format: $1\"; return 1 ;;\n"
    "  esac\n"
    "}\n",

    /* history-search — 增强历史搜索 */
    "# @plugin history-search\n"
    "# @desc 增强历史搜索别名\n"
    "# @version 1.0\n"
    "# @author msh\n"
    "\n"
    "alias h='history'\n"
    "alias hg='history | grep'\n",

    NULL
};

static const char * const builtin_plugin_names[] = {
    "git", "exit-code", "virtualenv", "docker", "extract", "history-search", NULL
};

/* === 内置主题 === */
static const char * const builtin_theme_code[] = {
    /* modern — 简约现代风格（默认）
     * ➜ ~/src/msh (main) $
     * 绿色箭头 + 蓝色路径 + 青色 git + exit code */
    "# @theme modern\n"
    "# @desc 简约现代风格：彩色路径 + git 状态 + exit code\n"
    "# @colorscheme dark\n"
    "\n"
    "export MSH_PS1='\\[\\e[1;32m\\]\xe2\x9e\x9c\\[\\e[0m\\] \\[\\e[34m\\]\\w\\[\\e[0m\\]\\g \\p\\[\\e[1;32m\\]\\$\\[\\e[0m\\] '\n"
    "export MSH_PS2='\\[\\e[1;30m\\]> \\[\\e[0m\\]'\n",

    /* minimal — 极简纯文本 */
    "# @theme minimal\n"
    "# @desc 极简风格：纯文本\n"
    "# @colorscheme none\n"
    "\n"
    "export MSH_PS1='$ '\n"
    "export MSH_PS2='> '\n",

    /* colorful — 鲜艳多彩 */
    "# @theme colorful\n"
    "# @desc 鲜艳风格：多色提示符\n"
    "# @colorscheme dark\n"
    "\n"
    "export MSH_PS1='\\[\\e[1;32m\\]\\u@\\h\\[\\e[0m\\]:\\[\\e[1;34m\\]\\w\\[\\e[0m\\]\\g\\[\\e[1;33m\\]\\$\\[\\e[0m\\] '\n"
    "export MSH_PS2='\\[\\e[1;30m\\]> \\[\\e[0m\\]'\n",

    /* powerline — 箭头分隔符风格 */
    "# @theme powerline\n"
    "# @desc Powerline 风格：箭头分隔符\n"
    "# @colorscheme dark\n"
    "\n"
    "export MSH_PS1='\\[\\e[1;37;44m\\] \\w \\[\\e[0;44m\\]\\[\\e[1;37;41m\\]\\g \\[\\e[0;41m\\]\\[\\e[1;37;41m\\]$ \\[\\e[0m\\] '\n"
    "export MSH_PS2='\\[\\e[1;37;44m\\]> \\[\\e[0m\\]'\n",

    /* clean — 干净清爽风格
     * ~/src/msh $
     * 仅路径 + $，无颜色 */
    "# @theme clean\n"
    "# @desc 干净清爽风格：仅路径和提示符\n"
    "# @colorscheme light\n"
    "\n"
    "export MSH_PS1='\\w \\g\\$ '\n"
    "export MSH_PS2='  '\n",

    /* rainbow — 彩虹风格
     * 每个段落不同颜色 */
    "# @theme rainbow\n"
    "# @desc 彩虹风格：每段不同颜色\n"
    "# @colorscheme dark\n"
    "\n"
    "export MSH_PS1='\\[\\e[38;5;208m\\]\xe2\x9d\xaf\\[\\e[38;5;39m\\] \\w \\[\\e[38;5;114m\\]\\g\\[\\e[38;5;208m\\]\\$\\[\\e[0m\\] '\n"
    "export MSH_PS2='\\[\\e[38;5;240m\\]>\\[\\e[0m\\] '\n",

    NULL
};

static const char * const builtin_theme_names[] = {
    "modern", "minimal", "colorful", "powerline", "clean", "rainbow", NULL
};

/* === 插件扫描 === */
int msh_plugin_scan(void) {
    num_plugins = 0;

    /* 注册内置插件 */
    for (int i = 0; builtin_plugin_names[i]; i++) {
        if (num_plugins >= MAX_PLUGINS) break;
        plugin_entry_t *e = &plugins[num_plugins++];
        memset(e, 0, sizeof(*e));
        snprintf(e->name, sizeof(e->name), "%s", builtin_plugin_names[i]);
        e->path[0] = '\0';
        e->enabled = 0;
        e->builtin = 1;
        parse_meta(builtin_plugin_code[i], &e->meta);
    }

    /* 扫描用户插件目录 */
    const char *dir = get_plugin_dir();
    DIR *d = opendir(dir);
    if (!d) return num_plugins;

    struct dirent *de;
    while ((de = readdir(d)) != NULL && num_plugins < MAX_PLUGINS) {
        if (de->d_name[0] == '.') continue;

        char fpath[PATH_MAX];
        snprintf(fpath, sizeof(fpath), "%s/%s", dir, de->d_name);

        struct stat st;
        if (stat(fpath, &st) != 0) continue;

        char *code = NULL;
        char plugin_name[64] = {0};

        if (S_ISDIR(st.st_mode)) {
            /* 目录插件：init.msh */
            char initfile[PATH_MAX];
            snprintf(initfile, sizeof(initfile), "%s/init.msh", fpath);
            if (access(initfile, R_OK) != 0) continue;

            FILE *f = fopen(initfile, "r");
            if (!f) continue;
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            code = malloc(sz + 1);
            size_t nread = fread(code, 1, sz, f);
            code[nread] = '\0';
            fclose(f);

            snprintf(plugin_name, sizeof(plugin_name), "%s", de->d_name);
        } else if (S_ISREG(st.st_mode)) {
            /* 文件插件：*.msh */
            const char *ext = strrchr(de->d_name, '.');
            if (!ext || strcmp(ext, ".msh") != 0) continue;

            FILE *f = fopen(fpath, "r");
            if (!f) continue;
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            code = malloc(sz + 1);
            size_t nread = fread(code, 1, sz, f);
            code[nread] = '\0';
            fclose(f);

            size_t nl = ext - de->d_name;
            if (nl >= sizeof(plugin_name)) nl = sizeof(plugin_name) - 1;
            memcpy(plugin_name, de->d_name, nl);
            plugin_name[nl] = '\0';
        } else {
            continue;
        }

        if (code) {
            plugin_entry_t *e = &plugins[num_plugins++];
            memset(e, 0, sizeof(*e));
            snprintf(e->name, sizeof(e->name), "%s", plugin_name);
            snprintf(e->path, sizeof(e->path), "%s", fpath);
            e->enabled = 1;
            e->builtin = 0;
            parse_meta(code, &e->meta);
            free(code);
        }
    }
    closedir(d);
    return num_plugins;
}

/* === 主题扫描 === */
static int theme_scan(void) {
    num_themes = 0;

    /* 注册内置主题 */
    for (int i = 0; builtin_theme_names[i]; i++) {
        if (num_themes >= MAX_THEMES) break;
        theme_entry_t *e = &themes[num_themes++];
        memset(e, 0, sizeof(*e));
        snprintf(e->name, sizeof(e->name), "%s", builtin_theme_names[i]);
        e->path[0] = '\0';
        e->builtin = 1;
        parse_meta(builtin_theme_code[i], &e->meta);
    }

    /* 扫描用户主题目录 */
    const char *dir = get_theme_dir();
    DIR *d = opendir(dir);
    if (!d) return num_themes;

    struct dirent *de;
    while ((de = readdir(d)) != NULL && num_themes < MAX_THEMES) {
        const char *ext = strrchr(de->d_name, '.');
        if (!ext || strcmp(ext, ".msh") != 0) continue;

        char fpath[PATH_MAX];
        snprintf(fpath, sizeof(fpath), "%s/%s", dir, de->d_name);

        FILE *f = fopen(fpath, "r");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *code = malloc(sz + 1);
        size_t nread = fread(code, 1, sz, f);
        code[nread] = '\0';
        fclose(f);

        theme_entry_t *e = &themes[num_themes++];
        memset(e, 0, sizeof(*e));
        char name[64];
        size_t nl = ext - de->d_name;
        if (nl >= sizeof(name)) nl = sizeof(name) - 1;
        memcpy(name, de->d_name, nl);
        name[nl] = '\0';
        snprintf(e->name, sizeof(e->name), "%s", name);
        snprintf(e->path, sizeof(e->path), "%s", fpath);
        e->builtin = 0;
        parse_meta(code, &e->meta);
        free(code);
    }
    closedir(d);
    return num_themes;
}

/* === 插件加载 === */
int msh_plugin_load(const char *name) {
    /* 查内置插件 */
    for (int i = 0; builtin_plugin_names[i]; i++) {
        if (strcmp(builtin_plugin_names[i], name) == 0) {
            return msh_run_string(builtin_plugin_code[i],
                                  strlen(builtin_plugin_code[i])) == 0 ? 0 : -1;
        }
    }

    /* 查用户插件 */
    for (int i = 0; i < num_plugins; i++) {
        if (strcmp(plugins[i].name, name) == 0 && !plugins[i].builtin) {
            return source_file(plugins[i].path) == 0 ? 0 : -1;
        }
    }

    /* 尝试直接路径 */
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s/init.msh", get_plugin_dir(), name);
    if (access(path, R_OK) == 0) return source_file(path);
    snprintf(path, sizeof(path), "%s/%s.msh", get_plugin_dir(), name);
    if (access(path, R_OK) == 0) return source_file(path);

    fprintf(stderr, "msh: %s: %s\n", msh_i18n(MSG_PLUGIN_NOT_FOUND), name);
    return -1;
}

int msh_plugin_list(void) {
    printf("%s", msh_i18n(MSG_PLUGIN_LIST_HEADER));
    for (int i = 0; i < num_plugins; i++) {
        printf("  %s %-16s %s",
               plugins[i].enabled ? "[+]" : "[ ]",
               plugins[i].name,
               plugins[i].meta.desc[0] ? plugins[i].meta.desc : "");
        if (plugins[i].builtin) printf("  \033[90m(内置)\033[0m");
        else printf("  \033[90m(用户)\033[0m");
        printf("\n");
    }
    if (num_plugins == 0) {
        printf(msh_i18n(MSG_PLUGIN_NONE), get_plugin_dir());
    }
    return 0;
}

/* 显示插件详情 */
static int plugin_info(const char *name) {
    for (int i = 0; i < num_plugins; i++) {
        if (strcmp(plugins[i].name, name) == 0) {
            printf("名称:    %s\n", plugins[i].name);
            printf("描述:    %s\n", plugins[i].meta.desc[0] ? plugins[i].meta.desc : "(无)");
            if (plugins[i].meta.version[0])
                printf("版本:    %s\n", plugins[i].meta.version);
            if (plugins[i].meta.author[0])
                printf("作者:    %s\n", plugins[i].meta.author);
            printf("类型:    %s\n", plugins[i].builtin ? "内置" : "用户");
            printf("状态:    %s\n", plugins[i].enabled ? "已启用" : "未启用");
            if (!plugins[i].builtin && plugins[i].path[0])
                printf("路径:    %s\n", plugins[i].path);
            return 0;
        }
    }
    fprintf(stderr, "msh: %s: %s\n", msh_i18n(MSG_PLUGIN_NOT_FOUND), name);
    return 1;
}

int msh_plugin_enable(const char *name) {
    for (int i = 0; i < num_plugins; i++) {
        if (strcmp(plugins[i].name, name) == 0) {
            plugins[i].enabled = 1;
            return msh_plugin_load(name) == 0 ? 0 : 1;
        }
    }
    fprintf(stderr, "msh: %s: %s\n", msh_i18n(MSG_PLUGIN_NOT_FOUND), name);
    return -1;
}

int msh_plugin_disable(const char *name) {
    for (int i = 0; i < num_plugins; i++) {
        if (strcmp(plugins[i].name, name) == 0) {
            plugins[i].enabled = 0;
            printf(msh_i18n(MSG_PLUGIN_DISABLED), name);
            return 0;
        }
    }
    fprintf(stderr, "msh: %s: %s\n", msh_i18n(MSG_PLUGIN_NOT_FOUND), name);
    return -1;
}

/* === 主题系统 === */
int msh_theme_list(void) {
    theme_scan();
    printf("%s", msh_i18n(MSG_THEME_LIST_HEADER));
    for (int i = 0; i < num_themes; i++) {
        printf("  %-12s %s", themes[i].name,
               themes[i].meta.desc[0] ? themes[i].meta.desc : "");
        if (themes[i].builtin) printf("  \033[90m(内置)\033[0m");
        printf("\n");
    }
    return 0;
}

static int theme_info(const char *name) {
    for (int i = 0; i < num_themes; i++) {
        if (strcmp(themes[i].name, name) == 0) {
            printf("名称:    %s\n", themes[i].name);
            printf("描述:    %s\n", themes[i].meta.desc[0] ? themes[i].meta.desc : "(无)");
            printf("类型:    %s\n", themes[i].builtin ? "内置" : "用户");
            if (!themes[i].builtin && themes[i].path[0])
                printf("路径:    %s\n", themes[i].path);
            return 0;
        }
    }
    fprintf(stderr, "msh: %s: %s\n", msh_i18n(MSG_THEME_NOT_FOUND), name);
    return 1;
}

int msh_theme_apply(const char *name) {
    /* 1. 尝试内置 YAML 主题（优先，更强大） */
    if (msh_theme_try_yaml_builtin(name) == 0) return 0;

    /* 2. 查内置 .msh 主题 */
    for (int i = 0; builtin_theme_names[i]; i++) {
        if (strcmp(builtin_theme_names[i], name) == 0) {
            int ret = msh_run_string(builtin_theme_code[i],
                                     strlen(builtin_theme_code[i]));
            if (ret == 0) setenv("MSH_THEME", name, 1);
            return ret;
        }
    }

    /* 3. 查用户主题 */
    for (int i = 0; i < num_themes; i++) {
        if (strcmp(themes[i].name, name) == 0 && !themes[i].builtin) {
            /* 尝试 YAML 格式 */
            if (msh_theme_apply_yaml_file(themes[i].path, name) == 0) return 0;
            /* 回退到 .msh 脚本 */
            int ret = source_file(themes[i].path);
            if (ret == 0) setenv("MSH_THEME", name, 1);
            return ret;
        }
    }

    /* 4. 尝试直接路径 */
    char path[PATH_MAX];
    /* 先试 .yaml */
    snprintf(path, sizeof(path), "%s/%s.yaml", get_theme_dir(), name);
    if (access(path, R_OK) == 0) {
        return msh_theme_apply_yaml_file(path, name);
    }
    /* 再试 .yml */
    snprintf(path, sizeof(path), "%s/%s.yml", get_theme_dir(), name);
    if (access(path, R_OK) == 0) {
        return msh_theme_apply_yaml_file(path, name);
    }
    /* 再试 .msh */
    snprintf(path, sizeof(path), "%s/%s.msh", get_theme_dir(), name);
    if (access(path, R_OK) == 0) {
        int ret = source_file(path);
        if (ret == 0) setenv("MSH_THEME", name, 1);
        return ret;
    }
    if (access(name, R_OK) == 0) {
        int ret = source_file(name);
        if (ret == 0) setenv("MSH_THEME", name, 1);
        return ret;
    }

    fprintf(stderr, "msh: %s: %s\n", msh_i18n(MSG_THEME_NOT_FOUND), name);
    return -1;
}

int msh_theme_current(void) {
    const char *theme = getenv("MSH_THEME");
    if (theme) {
        printf(msh_i18n(MSG_THEME_CURRENT), theme);
    } else {
        fputs(msh_i18n(MSG_THEME_NONE_LOADED), stdout);
    }
    return 0;
}

/* 兼容旧接口 */
int msh_theme_builtin(const char *name) {
    return msh_theme_apply(name);
}

/* === msh plugin 内建命令 ===
 *
 * 用法:
 *   msh plugin list                     列出所有插件
 *   msh plugin load <name>              加载插件
 *   msh plugin enable <name>            启用插件
 *   msh plugin disable <name>           禁用插件
 *   msh plugin info <name>              显示插件详情
 *   msh plugin theme list               列出所有主题
 *   msh plugin theme apply <name>       应用主题
 *   msh plugin theme current            显示当前主题
 *   msh plugin theme info <name>        显示主题详情
 *   msh plugin lang [zh-CN|en-US]       设置/查看语言
 *   msh plugin compat bash|zsh          导入 bash/zsh 配置
 *   msh plugin init <name>              创建插件模板
 *   msh plugin init-theme <name>        创建主题模板
 */
int msh_plugin_builtin(int argc, char **argv) {
    int ai = 1;
    if (ai < argc && strcmp(argv[ai], "plugin") == 0) ai++;

    if (ai >= argc) {
        fprintf(stderr,
            "usage: msh plugin <command> [args]\n"
            "\n"
            "插件命令:\n"
            "  list              列出所有插件\n"
            "  load <name>       加载插件\n"
            "  enable <name>     启用插件\n"
            "  disable <name>    禁用插件\n"
            "  info <name>       显示插件详情\n"
            "\n"
            "主题命令:\n"
            "  theme list              列出所有主题\n"
            "  theme apply <name>      应用主题\n"
            "  theme current           显示当前主题\n"
            "  theme info <name>       显示主题详情\n"
            "\n"
            "其他:\n"
            "  lang [zh-CN|en-US]     设置/查看语言\n"
            "  compat bash|zsh        导入 bash/zsh 配置\n"
            "  init <name>            创建插件模板\n"
            "  init-theme <name>      创建主题模板\n");
        return 1;
    }

    const char *sub = argv[ai];
    int sub_argc = argc - ai;
    char **sub_argv = &argv[ai];

    /* --- 插件命令 --- */
    if (strcmp(sub, "list") == 0) {
        msh_plugin_scan();
        return msh_plugin_list();
    }
    if (strcmp(sub, "load") == 0 || strcmp(sub, "enable") == 0) {
        if (sub_argc < 2) {
            fprintf(stderr, "msh: plugin %s: name required\n", sub);
            return 1;
        }
        msh_plugin_scan();
        return msh_plugin_enable(sub_argv[1]) == 0 ? 0 : 1;
    }
    if (strcmp(sub, "disable") == 0) {
        if (sub_argc < 2) {
            fprintf(stderr, "msh: plugin disable: name required\n");
            return 1;
        }
        msh_plugin_scan();
        return msh_plugin_disable(sub_argv[1]) == 0 ? 0 : 1;
    }
    if (strcmp(sub, "info") == 0) {
        if (sub_argc < 2) {
            fprintf(stderr, "msh: plugin info: name required\n");
            return 1;
        }
        msh_plugin_scan();
        return plugin_info(sub_argv[1]);
    }

    /* --- 主题命令 --- */
    if (strcmp(sub, "theme") == 0) {
        if (sub_argc < 2) {
            return msh_theme_list();
        }
        const char *tsub = sub_argv[1];
        if (strcmp(tsub, "list") == 0) {
            return msh_theme_list();
        }
        if (strcmp(tsub, "apply") == 0) {
            if (sub_argc < 3) {
                fprintf(stderr, "msh: plugin theme apply: name required\n");
                return 1;
            }
            return msh_theme_apply(sub_argv[2]) == 0 ? 0 : 1;
        }
        if (strcmp(tsub, "current") == 0) {
            return msh_theme_current();
        }
        if (strcmp(tsub, "info") == 0) {
            if (sub_argc < 3) {
                fprintf(stderr, "msh: plugin theme info: name required\n");
                return 1;
            }
            theme_scan();
            return theme_info(sub_argv[2]);
        }
        /* "msh plugin theme <name>" → apply */
        return msh_theme_apply(tsub) == 0 ? 0 : 1;
    }

    /* --- 语言命令 --- */
    if (strcmp(sub, "lang") == 0) {
        if (sub_argc < 2) {
            msh_i18n_list_langs();
            printf("\n当前: %s\n", msh_i18n_lang_name(msh_i18n_lang()));
            return 0;
        }
        const char *l = sub_argv[1];
        if (strcmp(l, "zh-CN") == 0 || strcmp(l, "zh") == 0) {
            msh_i18n_set_lang(MSH_LANG_ZH_CN);
            setenv("MSH_LANG", "zh-CN", 1);
            printf("语言已切换: 简体中文\n");
        } else if (strcmp(l, "en-US") == 0 || strcmp(l, "en") == 0) {
            msh_i18n_set_lang(MSH_LANG_EN_US);
            setenv("MSH_LANG", "en-US", 1);
            printf("Language: English (US)\n");
        } else {
            fprintf(stderr, "msh: unsupported language: %s\n", l);
            msh_i18n_list_langs();
            return 1;
        }
        return 0;
    }

    /* --- 兼容导入 --- */
    if (strcmp(sub, "compat") == 0) {
        if (sub_argc < 2) {
            fprintf(stderr, "msh: plugin compat: target required (bash|zsh)\n");
            return 1;
        }
        extern int msh_compat_import(const char *target);
        return msh_compat_import(sub_argv[1]);
    }

    /* --- 创建插件模板 --- */
    if (strcmp(sub, "init") == 0) {
        if (sub_argc < 2) {
            fprintf(stderr, "msh: plugin init: name required\n");
            return 1;
        }
        const char *pname = sub_argv[1];
        const char *home = getenv("HOME");
        if (!home) { fprintf(stderr, "msh: HOME not set\n"); return 1; }
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/.msh/plugins/%s.msh", home, pname);
        /* 确保目录存在 */
        char cmd[PATH_MAX];
        snprintf(cmd, sizeof(cmd), "%s/.msh/plugins", home);
        mkdir(cmd, 0755);
        snprintf(cmd, sizeof(cmd), "%s/.msh", home);
        mkdir(cmd, 0755);

        FILE *f = fopen(path, "w");
        if (!f) { fprintf(stderr, "msh: cannot create %s\n", path); return 1; }
        fprintf(f,
            "# @plugin %s\n"
            "# @desc 在此填写插件描述\n"
            "# @version 1.0\n"
            "# @author your-name\n"
            "\n"
            "# 在此添加别名、函数、补全等\n"
            "# alias ll='ls -l'\n"
            "# complete -c mycommand\n",
            pname);
        fclose(f);
        printf("插件模板已创建: %s\n", path);
        return 0;
    }

    /* --- 创建主题模板 --- */
    if (strcmp(sub, "init-theme") == 0) {
        if (sub_argc < 2) {
            fprintf(stderr, "msh: plugin init-theme: name required\n");
            return 1;
        }
        const char *tname = sub_argv[1];
        const char *home = getenv("HOME");
        if (!home) { fprintf(stderr, "msh: HOME not set\n"); return 1; }
        char dir[PATH_MAX];
        snprintf(dir, sizeof(dir), "%s/.msh", home); mkdir(dir, 0755);
        snprintf(dir, sizeof(dir), "%s/.msh/themes", home); mkdir(dir, 0755);
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/.msh/themes/%s.msh", home, tname);

        FILE *f = fopen(path, "w");
        if (!f) { fprintf(stderr, "msh: cannot create %s\n", path); return 1; }
        fprintf(f,
            "# @theme %s\n"
            "# @desc 在此填写主题描述\n"
            "# @colorscheme dark\n"
            "\n"
            "# PS1 转义序列:\n"
            "#   \\u 用户名  \\h 主机名  \\w 工作目录  \\W 目录名\n"
            "#   \\n 换行    \\t 时间    \\$ 提示符    \\g git状态\n"
            "#   \\p exit码  \\e ESC     \\[ \\] 不可见标记\n"
            "#   \\[\\e[1;32m\\] 粗体绿  \\[\\e[0m\\] 重置\n"
            "\n"
            "export MSH_PS1='\\[\\e[1;32m\\]\\u@\\h\\[\\e[0m\\]:\\[\\e[34m\\]\\w\\[\\e[0m\\]\\g\\$ '\n"
            "export MSH_PS2='> '\n",
            tname);
        fclose(f);
        printf("主题模板已创建: %s\n", path);
        return 0;
    }

    fprintf(stderr, "msh: plugin: unknown command '%s'\n", sub);
    fprintf(stderr, "usage: msh plugin {list|load|enable|disable|info|theme|lang|compat|init|init-theme}\n");
    return 1;
}
