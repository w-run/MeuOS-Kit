/* msh/exec/compctl.c — 补全脚本加载系统
 *
 * 提供 bash 风格的 complete/compgen 内建命令：
 *   complete -F func cmd    注册补全函数
 *   complete -c cmd          使用命令补全
 *   complete -f cmd          使用文件补全
 *   complete -p              列出所有已注册规则
 *   compgen -c word          生成命令补全候选
 *   compgen -f word          生成文件补全候选
 *
 * 补全脚本可通过 source 加载：
 *   source ~/.config/msh/completions/git.msh
 */

#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "msh/msh.h"

#define MAX_RULES 64

typedef enum {
    COMP_DEFAULT = 0,
    COMP_COMMAND = 1,
    COMP_FILE = 2,
    COMP_FUNC = 3,
} comp_type_t;

typedef struct {
    char *command;     /* command name to match */
    comp_type_t type;  /* completion type */
    char *func;        /* function name (for COMP_FUNC) */
} comp_rule_t;

static comp_rule_t rules[MAX_RULES];
static int rule_count = 0;

/* Find a rule for a given command name */
static comp_rule_t *find_rule(const char *cmd) {
    for (int i = 0; i < rule_count; i++) {
        if (strcmp(rules[i].command, cmd) == 0) return &rules[i];
    }
    return NULL;
}

/* Register or update a completion rule */
static int add_rule(const char *cmd, comp_type_t type, const char *func) {
    /* Check if rule already exists */
    comp_rule_t *r = find_rule(cmd);
    if (r) {
        r->type = type;
        free(r->func);
        r->func = func ? strdup(func) : NULL;
        return 0;
    }
    
    if (rule_count >= MAX_RULES) return -1;
    r = &rules[rule_count++];
    r->command = strdup(cmd);
    r->type = type;
    r->func = func ? strdup(func) : NULL;
    return 0;
}

/* Generate command completions */
static void gen_commands(const char *prefix) {
    size_t plen = strlen(prefix);
    
    /* Builtins */
    static const char *builtins[] = {
        "cd", "export", "unset", "set", "exit", "true", "false", ":",
        "echo", "pwd", "read", "eval", "type", "exec", "jobs", "fg", "bg",
        "wait", "trap", "source", ".", "complete", "compgen", NULL
    };
    for (int i = 0; builtins[i]; i++) {
        if (strncmp(builtins[i], prefix, plen) == 0)
            printf("%s\n", builtins[i]);
    }
    
    /* PATH commands */
    const char *path = getenv("PATH");
    if (!path) return;
    char *dup = strdup(path);
    char *save = NULL;
    char *dir = strtok_r(dup, ":", &save);
    while (dir) {
        DIR *d = opendir(dir);
        if (d) {
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL) {
                if (strncmp(ent->d_name, prefix, plen) != 0) continue;
                size_t n = strlen(dir) + 1 + strlen(ent->d_name) + 1;
                char *full = malloc(n);
                snprintf(full, n, "%s/%s", dir, ent->d_name);
                if (access(full, X_OK) == 0)
                    printf("%s\n", ent->d_name);
                free(full);
            }
            closedir(d);
        }
        dir = strtok_r(NULL, ":", &save);
    }
    free(dup);
}

/* Generate file completions */
static void gen_files(const char *prefix) {
    const char *slash = strrchr(prefix, '/');
    char dir[1024];
    char name[512];
    
    if (slash) {
        size_t dlen = (size_t)(slash - prefix);
        if (dlen >= sizeof(dir)) dlen = sizeof(dir) - 1;
        memcpy(dir, prefix, dlen);
        dir[dlen] = '\0';
        snprintf(name, sizeof(name), "%s", slash + 1);
    } else {
        snprintf(dir, sizeof(dir), ".");
        snprintf(name, sizeof(name), "%s", prefix);
    }
    
    size_t plen = strlen(name);
    DIR *d = opendir(dir);
    if (!d) return;
    
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (plen > 0 && strncmp(ent->d_name, name, plen) != 0) continue;
        if (ent->d_name[0] == '.' && !(plen > 0 && name[0] == '.')) continue;
        
        char fullpath[1536];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode))
            printf("%s/\n", ent->d_name);
        else
            printf("%s\n", ent->d_name);
    }
    closedir(d);
}

/* complete builtin */
int msh_builtin_complete(int argc, char **argv) {
    comp_type_t type = COMP_DEFAULT;
    const char *func = NULL;
    int opt_c = 0;
    
    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "-F") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "complete: -F requires function name\n");
                return 2;
            }
            type = COMP_FUNC;
            func = argv[i];
        } else if (strcmp(argv[i], "-c") == 0) {
            type = COMP_COMMAND;
        } else if (strcmp(argv[i], "-f") == 0) {
            type = COMP_FILE;
        } else if (strcmp(argv[i], "-p") == 0) {
            opt_c = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("complete: register completion rule\n"
                   "  complete -F func cmd   use function for completion\n"
                   "  complete -c cmd        use command completion\n"
                   "  complete -f cmd        use file completion\n"
                   "  complete -p            list all rules\n");
            return 0;
        }
        i++;
    }
    
    if (opt_c) {
        /* List all rules */
        for (int j = 0; j < rule_count; j++) {
            printf("complete");
            if (rules[j].type == COMP_FUNC)
                printf(" -F %s", rules[j].func ? rules[j].func : "");
            else if (rules[j].type == COMP_COMMAND)
                printf(" -c");
            else if (rules[j].type == COMP_FILE)
                printf(" -f");
            printf(" %s\n", rules[j].command);
        }
        return 0;
    }
    
    if (i >= argc) {
        fprintf(stderr, "complete: missing command name\n");
        return 2;
    }
    
    /* Register rule for each remaining argument */
    for (; i < argc; i++) {
        add_rule(argv[i], type, func);
    }
    
    return 0;
}

/* compgen builtin */
int msh_builtin_compgen(int argc, char **argv) {
    comp_type_t type = COMP_DEFAULT;
    int i = 1;
    
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "-c") == 0) {
            type = COMP_COMMAND;
        } else if (strcmp(argv[i], "-f") == 0) {
            type = COMP_FILE;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("compgen: generate completions\n"
                   "  compgen -c word    generate command completions\n"
                   "  compgen -f word    generate file completions\n");
            return 0;
        }
        i++;
    }
    
    if (i >= argc) {
        fprintf(stderr, "compgen: missing word\n");
        return 2;
    }
    
    const char *word = argv[i];
    
    if (type == COMP_COMMAND) {
        gen_commands(word);
    } else if (type == COMP_FILE) {
        gen_files(word);
    } else {
        /* Default: try files */
        gen_files(word);
    }
    
    return 0;
}

/* Query completion for a command in interactive mode.
 * Returns the completion type and optionally the function name.
 * Used by msh_complete to check if a custom rule exists. */
int msh_compctl_lookup(const char *cmd, int *type, const char **func) {
    comp_rule_t *r = find_rule(cmd);
    if (!r) return 0;
    *type = (int)r->type;
    *func = r->func;
    return 1;
}

/* Load completion scripts from a directory */
int msh_compctl_load_dir(const char *dirpath) {
    DIR *d = opendir(dirpath);
    if (!d) return -1;
    
    struct dirent *ent;
    char path[2048];
    while ((ent = readdir(d)) != NULL) {
        /* Only load .msh files */
        const char *dot = strrchr(ent->d_name, '.');
        if (!dot || strcmp(dot, ".msh") != 0) continue;
        
        snprintf(path, sizeof(path), "%s/%s", dirpath, ent->d_name);
        
        /* Source the file */
        extern int msh_run_string(const char *src, size_t len);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *buf = malloc(sz + 1);
        size_t nread = fread(buf, 1, sz, f);
        buf[nread] = '\0';
        fclose(f);
        
        msh_run_string(buf, nread);
        free(buf);
    }
    closedir(d);
    return 0;
}
