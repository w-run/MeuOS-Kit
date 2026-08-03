/* msh/exec/complete.c — Tab 补全引擎
 *
 * 提供命令名、文件路径、变量名的补全。
 * 设计原则：
 *   - 第一次 Tab：补全公共前缀
 *   - 连续 Tab（或无法进一步补全）：列出所有候选
 *   - 命令补全：内建 + PATH 中的可执行文件
 *   - 路径补全：文件系统
 *   - 变量补全：$VAR / ${VAR 形式的环境变量
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "msh/msh.h"
#include "msh/history.h"

#define MAX_CANDIDATES 512
#define MAX_PREFIX 256

/* 候选数组 */
typedef struct {
    char *items[MAX_CANDIDATES];
    int count;
    char prefix[MAX_PREFIX];
    int prefix_len;
    int is_cmd;     /* 1=命令补全, 0=路径补全, 2=变量补全 */
} completion_t;

static void cand_add(completion_t *c, const char *s) {
    if (c->count >= MAX_CANDIDATES) return;
    c->items[c->count++] = strdup(s);
}

static void cand_clear(completion_t *c) {
    for (int i = 0; i < c->count; i++) free(c->items[i]);
    c->count = 0;
    c->prefix[0] = '\0';
    c->prefix_len = 0;
    c->is_cmd = 0;
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

/* 内建命令列表 */
static const char *builtin_names[] = {
    "cd", "export", "unset", "set", "exit", "true", "false", ":",
    "echo", "pwd", "read", "eval", "type", "exec", "jobs", "fg", "bg",
    "wait", "trap", NULL
};

/* 收集内建命令候选 */
static void collect_builtins(completion_t *c, const char *prefix) {
    size_t plen = strlen(prefix);
    for (int i = 0; builtin_names[i]; i++) {
        if (strncmp(builtin_names[i], prefix, plen) == 0) {
            cand_add(c, builtin_names[i]);
        }
    }
}

/* 从 PATH 收集可执行文件 */
static void collect_path_cmds(completion_t *c, const char *prefix) {
    size_t plen = strlen(prefix);
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
                /* 检查是否可执行 */
                size_t n = strlen(dir) + 1 + strlen(ent->d_name) + 1;
                char *full = malloc(n);
                snprintf(full, n, "%s/%s", dir, ent->d_name);
                if (access(full, X_OK) == 0) {
                    /* 去重 */
                    int dup_found = 0;
                    for (int k = 0; k < c->count; k++) {
                        if (!strcmp(c->items[k], ent->d_name)) { dup_found = 1; break; }
                    }
                    if (!dup_found) cand_add(c, ent->d_name);
                }
                free(full);
            }
            closedir(d);
        }
        dir = strtok_r(NULL, ":", &save);
    }
    free(dup);
}

/* 收集文件路径候选 */
static void collect_files(completion_t *c, const char *prefix) {
    /* 分离目录和文件名前缀 */
    const char *slash = strrchr(prefix, '/');
    char dir[1024];
    char name_prefix[1024];
    if (slash) {
        size_t dlen = (size_t)(slash - prefix);
        if (dlen >= sizeof(dir)) dlen = sizeof(dir) - 1;
        memcpy(dir, prefix, dlen);
        dir[dlen] = '\0';
        size_t sl = strlen(slash + 1);
        if (sl >= sizeof(name_prefix)) sl = sizeof(name_prefix) - 1;
        memcpy(name_prefix, slash + 1, sl);
        name_prefix[sl] = '\0';
    } else {
        strcpy(dir, ".");
        size_t pl = strlen(prefix);
        if (pl >= sizeof(name_prefix)) pl = sizeof(name_prefix) - 1;
        memcpy(name_prefix, prefix, pl);
        name_prefix[pl] = '\0';
    }
    size_t plen = strlen(name_prefix);

    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (plen > 0 && strncmp(ent->d_name, name_prefix, plen) != 0) continue;
        if (ent->d_name[0] == '.' && !(plen > 0 && name_prefix[0] == '.')) continue;
        /* 构造完整路径候选 */
        size_t n = strlen(dir) + 1 + strlen(ent->d_name) + 2;
        char *cand = malloc(n);
        snprintf(cand, n, "%s/%s", dir, ent->d_name);
        /* 检查是否是目录，追加 / */
        struct stat st;
        if (stat(cand, &st) == 0 && S_ISDIR(st.st_mode)) {
            strcat(cand, "/");
        }
        /* 如果是相对路径的目录形式，去掉开头的 ./ */
        char *display = cand;
        if (strcmp(dir, ".") == 0) {
            display = cand + 2;  /* 跳过 "./" */
        }
        cand_add(c, display);
        free(cand);
    }
    closedir(d);
}

/* 收集变量名候选 */
static void collect_vars(completion_t *c, const char *prefix) {
    size_t plen = strlen(prefix);
    extern char **environ;
    for (char **e = environ; *e; e++) {
        const char *eq = strchr(*e, '=');
        if (!eq) continue;
        size_t nlen = (size_t)(eq - *e);
        if (nlen < plen) continue;
        if (strncmp(*e, prefix, plen) != 0) continue;
        char *vname = malloc(nlen + 1);
        memcpy(vname, *e, nlen);
        vname[nlen] = '\0';
        cand_add(c, vname);
        free(vname);
    }
}

/* 计算公共前缀 */
static void common_prefix(completion_t *c) {
    if (c->count == 0) return;
    if (c->count == 1) {
        snprintf(c->prefix, sizeof(c->prefix), "%s", c->items[0]);
        c->prefix_len = (int)strlen(c->prefix);
        return;
    }
    /* 对所有候选排序后比较第一个和最后一个 */
    qsort(c->items, (size_t)c->count, sizeof(char *), cmp_str);
    const char *first = c->items[0];
    const char *last = c->items[c->count - 1];
    int i = 0;
    while (first[i] && last[i] && first[i] == last[i]) i++;
    c->prefix_len = i;
    if (i >= (int)sizeof(c->prefix)) i = (int)sizeof(c->prefix) - 1;
    memcpy(c->prefix, first, (size_t)i);
    c->prefix[i] = '\0';
}

/* 在终端中列出候选（多列） */
static void show_candidates(completion_t *c) {
    if (c->count == 0) return;
    fprintf(stdout, "\n");
    /* 计算最大宽度 */
    int max_w = 0;
    for (int i = 0; i < c->count; i++) {
        int w = (int)strlen(c->items[i]);
        if (w > max_w) max_w = w;
    }
    /* 获取终端宽度（简化：默认80） */
    int term_w = 80;
    const char *cw = getenv("COLUMNS");
    if (cw) term_w = atoi(cw);
    int col_w = max_w + 2;
    if (col_w < 1) col_w = 1;
    int cols = term_w / col_w;
    if (cols < 1) cols = 1;
    int rows = (c->count + cols - 1) / cols;
    for (int r = 0; r < rows; r++) {
        for (int cc = 0; cc < cols; cc++) {
            int idx = cc * rows + r;
            if (idx >= c->count) break;
            if (cc > 0) fprintf(stdout, "  ");
            fprintf(stdout, "%-*s", col_w, c->items[idx]);
        }
        fprintf(stdout, "\n");
    }
    fflush(stdout);
}

/* 主入口：尝试补全当前行。
 * buf 是当前编辑缓冲区，cur 是光标位置，cap 是缓冲区容量。
 * is_continuous_tab 表示上一次按键也是 Tab（用于触发列表显示）。
 * 返回 1 表示已处理（需刷新行显示），0 表示无补全。
 * 补全后的内容写入 buf（确保以 \0 结尾，更新 *cur 和 *len）。 */
int msh_complete(char *buf, size_t *cur, size_t *len, size_t cap, int is_continuous_tab) {
    /* 找到当前 word 的起始位置 */
    size_t word_start = *cur;
    while (word_start > 0 && buf[word_start - 1] != ' ' && buf[word_start - 1] != '\t'
           && buf[word_start - 1] != '|' && buf[word_start - 1] != '&'
           && buf[word_start - 1] != ';') {
        word_start--;
    }
    size_t word_len = *cur - word_start;
    if (word_len == 0 && !is_continuous_tab) return 0;  /* 空 word，首次 Tab 不动作 */

    /* 提取当前 word */
    char word[1024];
    if (word_len >= sizeof(word)) word_len = sizeof(word) - 1;
    memcpy(word, buf + word_start, word_len);
    word[word_len] = '\0';

    completion_t c;
    memset(&c, 0, sizeof(c));

    if (word[0] == '$') {
        /* 变量补全：$VAR 或 ${VAR */
        c.is_cmd = 2;
        char *vp = word + 1;
        if (*vp == '{') vp++;
        /* 剥除可能的 } */
        char var_prefix[1024];
        size_t vpl = strlen(vp);
        if (vpl >= sizeof(var_prefix)) vpl = sizeof(var_prefix) - 1;
        memcpy(var_prefix, vp, vpl);
        var_prefix[vpl] = '\0';
        int vl = (int)strlen(var_prefix);
        if (vl > 0 && var_prefix[vl - 1] == '}') var_prefix[vl - 1] = '\0';
        collect_vars(&c, var_prefix);
    } else if (word_start == 0 || (word_start == 1 && buf[0] == ' ')) {
        /* 命令补全：行首第一个 word */
        c.is_cmd = 1;
        collect_builtins(&c, word);
        collect_path_cmds(&c, word);
    } else {
        /* 路径补全 */
        c.is_cmd = 0;
        collect_files(&c, word);
    }

    if (c.count == 0) {
        /* 无候选：响铃 */
        if (!is_continuous_tab) {
            fputc('\a', stdout);
            fflush(stdout);
        }
        return 0;
    }

    qsort(c.items, (size_t)c.count, sizeof(char *), cmp_str);
    common_prefix(&c);

    if (c.count == 1) {
        /* 唯一候选：补全 + 空格（命令/变量后不加空格如果是目录）*/
        const char *cand = c.items[0];
        size_t clen = strlen(cand);
        int add_space = (c.is_cmd == 0 && cand[clen - 1] != '/');
        if (c.is_cmd == 1) add_space = 1;
        if (c.is_cmd == 2) add_space = 0;
        size_t insert_len = clen + (add_space ? 1 : 0);
        /* 检查容量 */
        if (*len + insert_len + 1 >= cap) {
            cap = cap * 2 + insert_len;
            /* 注意：这里无法 realloc 调用者的 buf，要求调用者给足空间 */
        }
        /* 替换 word 部分 */
        /* 先将 word 后面的内容后移 */
        size_t tail_len = *len - *cur;
        size_t new_cur = word_start + insert_len;
        if (tail_len > 0 && new_cur != *cur) {
            memmove((char *)buf + new_cur, buf + *cur, tail_len);
        }
        memcpy((char *)buf + word_start, cand, clen);
        if (add_space) {
            ((char *)buf)[word_start + clen] = ' ';
        }
        *len = *len - word_len + insert_len;
        *cur = word_start + insert_len;
        buf[*len] = '\0';
        cand_clear(&c);
        return 1;
    }

    /* 多个候选 */
    if (c.prefix_len > (int)word_len) {
        /* 可以补全公共前缀 */
        size_t insert_len = (size_t)c.prefix_len;
        size_t tail_len = *len - *cur;
        if (tail_len > 0) {
            memmove((char *)buf + word_start + insert_len, buf + *cur, tail_len);
        }
        memcpy((char *)buf + word_start, c.prefix, insert_len);
        *len = *len - word_len + insert_len;
        *cur = word_start + insert_len;
        buf[*len] = '\0';
        cand_clear(&c);
        return 1;
    }

    /* 无法进一步补全：显示列表（仅在连续 Tab 或二次 Tab）*/
    if (is_continuous_tab || 1) {
        show_candidates(&c);
        /* 显示列表后，重绘当前行由调用者负责 */
    }
    cand_clear(&c);
    return 2;  /* 2 表示已显示列表 */
}
