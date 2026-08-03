/* msh/exec/history.c — 历史记录实现
 *
 * 简单的内存环形 + 追加写 ~/.msh_history。
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "msh/history.h"

static char *g_hist[MSH_HIST_MAX];
static int g_hist_n = 0;
static int g_loading = 0;  /* 加载期间不写文件 */

static char *hist_path(void) {
    const char *home = getenv("HOME");
    if (!home || !*home) return NULL;
    size_t len = strlen(home) + 16;
    char *p = malloc(len);
    if (!p) return NULL;
    snprintf(p, len, "%s/.msh_history", home);
    return p;
}

void msh_history_load(void) {
    char *path = hist_path();
    if (!path) return;
    /* 历史文件过大时截断（只保留尾部内容太复杂，直接清空重建） */
    FILE *fp = fopen(path, "r");
    if (!fp) { free(path); return; }
    fseek(fp, 0, SEEK_END);
    long fsz = ftell(fp);
    fclose(fp);
    if (fsz > 1024 * 1024) {
        /* 截断：删掉重建（保留最后一行之前的内容会丢失，简单起见清空） */
        unlink(path);
        free(path);
        return;
    }
    fp = fopen(path, "r");
    if (!fp) { free(path); return; }
    g_loading = 1;
    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        size_t L = strlen(line);
        if (L && line[L - 1] == '\n') line[--L] = '\0';
        if (!*line) continue;
        msh_history_add(line);
    }
    g_loading = 0;
    fclose(fp);
    free(path);
}

void msh_history_add(const char *line) {
    if (!line || !*line) return;
    /* 与上一条相同则跳过 */
    if (g_hist_n > 0 && strcmp(g_hist[g_hist_n - 1], line) == 0) return;
    if (g_hist_n == MSH_HIST_MAX) {
        /* 环形：移出最旧 */
        free(g_hist[0]);
        memmove(g_hist, g_hist + 1, (MSH_HIST_MAX - 1) * sizeof(char *));
        g_hist_n = MSH_HIST_MAX - 1;
    }
    g_hist[g_hist_n++] = strdup(line);

    /* 持久化（加载期间不写，避免读一条写一条） */
    if (g_loading) return;
    char *path = hist_path();
    if (!path) return;
    FILE *fp = fopen(path, "a");
    if (fp) {
        fputs(line, fp);
        fputc('\n', fp);
        fclose(fp);
    }
    free(path);
}

const char *msh_history_get(int idx) {
    if (idx < 0 || idx >= g_hist_n) return NULL;
    return g_hist[idx];
}

int msh_history_count(void) {
    return g_hist_n;
}
