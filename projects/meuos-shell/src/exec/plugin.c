/* msh/exec/plugin.c — zsh 风格插件/主题系统
 *
 * 提供轻量级插件管理和主题引擎：
 * - 插件目录扫描（~/.msh/plugins/）
 * - 主题加载（~/.msh/themes/）
 * - 插件 enable/disable/list
 * - 主题切换和应用
 *
 * 插件格式：每个插件是一个目录，包含 init.msh（初始化脚本）
 * 主题格式：每个主题是一个 .msh 脚本，设置 PS1/PS2 等变量
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
#include "msh/parse.h"
#include "msh/plugin.h"

#define MAX_PLUGINS 64
#define MAX_THEMES 32

/* static const char *prog = "msh-plugin";  — unused, removed to fix -Wunused-variable */

typedef struct {
    char name[64];
    char path[PATH_MAX];
    int enabled;
} plugin_entry_t;

static plugin_entry_t plugins[MAX_PLUGINS];
static int num_plugins = 0;

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

/* Helper: source a file by reading it and running it as a string */
static int source_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    /* Read entire file */
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

/* Scan plugin directory */
int msh_plugin_scan(void) {
    const char *dir = get_plugin_dir();
    DIR *d = opendir(dir);
    if (!d) return 0;
    
    num_plugins = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && num_plugins < MAX_PLUGINS) {
        if (e->d_name[0] == '.') continue;
        
        char fpath[PATH_MAX];
        int _n = snprintf(fpath, sizeof(fpath), "%s/%s", dir, e->d_name);
        if (_n < 0 || (size_t)_n >= sizeof(fpath)) continue;
        
        struct stat st;
        if (stat(fpath, &st) != 0) continue;
        
        if (S_ISDIR(st.st_mode)) {
            /* Check for init.msh */
            char initfile[PATH_MAX];
            int _n2 = snprintf(initfile, sizeof(initfile), "%s/init.msh", fpath);
            if (_n2 < 0 || (size_t)_n2 >= sizeof(initfile)) continue;
            if (access(initfile, R_OK) == 0) {
                int _nn = snprintf(plugins[num_plugins].name, sizeof(plugins[0].name), "%s", e->d_name);
                if (_nn < 0 || (size_t)_nn >= sizeof(plugins[0].name)) _nn = 0;
                plugins[num_plugins].name[_nn] = '\0';
                int _np = snprintf(plugins[num_plugins].path, sizeof(plugins[0].path), "%s", fpath);
                if (_np < 0 || (size_t)_np >= sizeof(plugins[0].path)) _np = 0;
                plugins[num_plugins].path[_np] = '\0';
                plugins[num_plugins].enabled = 1;
                num_plugins++;
            }
        } else if (S_ISREG(st.st_mode)) {
            /* Single .msh file as plugin */
            const char *ext = strrchr(e->d_name, '.');
            if (ext && strcmp(ext, ".msh") == 0) {
                char name[64];
                size_t nl = ext - e->d_name;
                if (nl >= sizeof(name)) nl = sizeof(name) - 1;
                memcpy(name, e->d_name, nl);
                name[nl] = '\0';
                
                snprintf(plugins[num_plugins].name, sizeof(plugins[0].name), "%s", name);
                plugins[num_plugins].name[sizeof(plugins[0].name)-1] = '\0';
                snprintf(plugins[num_plugins].path, sizeof(plugins[0].path), "%s", fpath);
                plugins[num_plugins].path[sizeof(plugins[0].path)-1] = '\0';
                plugins[num_plugins].enabled = 1;
                num_plugins++;
            }
        }
    }
    closedir(d);
    return num_plugins;
}

/* Load a single plugin by name */
int msh_plugin_load(const char *name) {
    for (int i = 0; i < num_plugins; i++) {
        if (strcmp(plugins[i].name, name) == 0) {
            char initfile[PATH_MAX];
            snprintf(initfile, sizeof(initfile), "%s/init.msh", plugins[i].path);
            return source_file(initfile) == 0 ? 0 : -1;
        }
    }
    /* Try as direct file path */
    char initfile[PATH_MAX];
    int n = snprintf(initfile, sizeof(initfile), "%s/%s/init.msh", get_plugin_dir(), name);
    if (n < 0 || (size_t)n >= sizeof(initfile)) { fprintf(stderr, "msh: plugin path too long\n"); return -1; }
    if (access(initfile, R_OK) == 0) {
        return source_file(initfile) == 0 ? 0 : -1;
    }
    /* Try as .msh file */
    n = snprintf(initfile, sizeof(initfile), "%s/%s.msh", get_plugin_dir(), name);
    if (n < 0 || (size_t)n >= sizeof(initfile)) { fprintf(stderr, "msh: plugin path too long\n"); return -1; }
    if (access(initfile, R_OK) == 0) {
        return source_file(initfile) == 0 ? 0 : -1;
    }
    fprintf(stderr, "msh: plugin not found: %s\n", name);
    return -1;
}

/* List all available plugins */
int msh_plugin_list(void) {
    printf("Available plugins:\n");
    for (int i = 0; i < num_plugins; i++) {
        printf("  %s %s\n", plugins[i].enabled ? "[+]" : "[ ]",
               plugins[i].name);
    }
    if (num_plugins == 0) {
        printf("  (none found in %s)\n", get_plugin_dir());
    }
    return 0;
}

/* Enable a plugin */
int msh_plugin_enable(const char *name) {
    for (int i = 0; i < num_plugins; i++) {
        if (strcmp(plugins[i].name, name) == 0) {
            plugins[i].enabled = 1;
            return msh_plugin_load(name);
        }
    }
    fprintf(stderr, "msh: plugin not found: %s\n", name);
    return -1;
}

/* Disable a plugin */
int msh_plugin_disable(const char *name) {
    for (int i = 0; i < num_plugins; i++) {
        if (strcmp(plugins[i].name, name) == 0) {
            plugins[i].enabled = 0;
            printf("Plugin '%s' disabled (reload shell to take effect)\n", name);
            return 0;
        }
    }
    fprintf(stderr, "msh: plugin not found: %s\n", name);
    return -1;
}

/* === 主题系统 === */

/* List available themes */
int msh_theme_list(void) {
    const char *dir = get_theme_dir();
    DIR *d = opendir(dir);
    if (!d) {
        printf("No themes found (directory %s does not exist)\n", dir);
        return 0;
    }
    
    printf("Available themes:\n");
    struct dirent *e;
    int count = 0;
    while ((e = readdir(d)) != NULL) {
        const char *ext = strrchr(e->d_name, '.');
        if (ext && strcmp(ext, ".msh") == 0) {
            char name[64];
            size_t nl = ext - e->d_name;
            if (nl >= sizeof(name)) nl = sizeof(name) - 1;
            memcpy(name, e->d_name, nl);
            name[nl] = '\0';
            printf("  %s\n", name);
            count++;
        }
    }
    if (count == 0) {
        printf("  (none found in %s)\n", dir);
    }
    closedir(d);
    return 0;
}

/* Apply a theme by name */
int msh_theme_apply(const char *name) {
    char path[PATH_MAX];
    
    int n = snprintf(path, sizeof(path), "%s/%s.msh", get_theme_dir(), name);
    if (n < 0 || (size_t)n >= sizeof(path)) { fprintf(stderr, "msh: theme path too long\n"); return -1; }
    if (access(path, R_OK) != 0) {
        /* Try as full path */
        if (access(name, R_OK) == 0) {
            strncpy(path, name, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';
        } else {
            fprintf(stderr, "msh: theme not found: %s\n", name);
            return -1;
        }
    }
    
    printf("Loading theme: %s\n", name);
    int ret = source_file(path);
    if (ret == 0) {
        /* Save current theme name */
        setenv("MSH_THEME", name, 1);
    }
    return ret;
}

/* Show current theme */
int msh_theme_current(void) {
    const char *theme = getenv("MSH_THEME");
    if (theme) {
        printf("Current theme: %s\n", theme);
    } else {
        printf("No theme loaded (using default)\n");
    }
    return 0;
}

/* Built-in theme: minimal */
static const char *theme_minimal = 
    "PS1='%n@%m:%~%# '\n"
    "PS2='> '\n";

/* Built-in theme: colorful */
static const char *theme_colorful =
    "PS1='\\[\\e[1;32m\\]%n@%m\\[\\e[0m\\]:\\[\\e[1;34m\\]%~\\[\\e[0m\\]$ '\n"
    "PS2='\\[\\e[1;30m\\]> \\[\\e[0m\\]'\n";

/* Built-in theme: powerline-style */
static const char *theme_powerline =
    "PS1='\\[\\e[1;37;44m\\] %~ \\[\\e[0;44m\\]\\[\\e[1;37;41m\\] $ \\[\\e[0m\\] '\n"
    "PS2='\\[\\e[1;37;44m\\]> \\[\\e[0m\\]'\n";

/* Apply built-in theme */
int msh_theme_builtin(const char *name) {
    const char *theme = NULL;
    if (strcmp(name, "minimal") == 0) theme = theme_minimal;
    else if (strcmp(name, "colorful") == 0) theme = theme_colorful;
    else if (strcmp(name, "powerline") == 0) theme = theme_powerline;
    
    if (!theme) {
        fprintf(stderr, "msh: unknown built-in theme: %s\n", name);
        fprintf(stderr, "Available: minimal, colorful, powerline\n");
        return -1;
    }
    
    int ret = msh_run_string((char *)theme, strlen(theme));
    if (ret == 0) {
        setenv("MSH_THEME", name, 1);
        printf("Theme '%s' applied\n", name);
    }
    return ret;
}

/* Plugin builtin command: msh plugin <subcommand> [args] */
int msh_plugin_builtin(int argc, char **argv) {
    /* Expect: argv[0]="msh", argv[1]="plugin", argv[2]=subcommand */
    int ai = 1; /* skip "msh" */
    if (ai < argc && strcmp(argv[ai], "plugin") == 0) ai++; /* skip "plugin" */
    
    if (ai >= argc) {
        fprintf(stderr, "usage: msh plugin {list|load|enable|disable|theme} [name]\n");
        return 1;
    }
    
    const char *sub = argv[ai];
    /* Remaining args after subcommand */
    int sub_argc = argc - ai;
    char **sub_argv = &argv[ai];
    
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
    if (strcmp(sub, "theme") == 0) {
        if (sub_argc < 2) {
            return msh_theme_list();
        }
        const char *tname = sub_argv[1];
        /* Check built-in themes first */
        if (strcmp(tname, "minimal") == 0 || strcmp(tname, "colorful") == 0 ||
            strcmp(tname, "powerline") == 0) {
            return msh_theme_builtin(tname) == 0 ? 0 : 1;
        }
        /* Try file-based theme */
        return msh_theme_apply(tname) == 0 ? 0 : 1;
    }
    if (strcmp(sub, "current-theme") == 0) {
        return msh_theme_current();
    }
    
    fprintf(stderr, "msh: plugin: unknown subcommand '%s'\n", sub);
    fprintf(stderr, "usage: msh plugin {list|load|enable|disable|theme} [name]\n");
    return 1;
}
