/* libutils/table.c — 自适应列宽表格 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "meuos/table.h"
#include "meuos/utils.h"

struct table_col {
    int width;       /* 0 = 自适应；>0 = 用户指定 */
    table_align_t align;
};

struct table_row {
    char **cells;
    int is_separator;
};

struct table {
    struct table_col *cols;
    size_t ncols;
    table_align_t default_align;
    struct table_row *rows;
    size_t nrows;
    size_t cap_rows;
    /* 已计算的实际宽度 */
    int *actual_widths;
};

table_t *table_new(size_t ncols, const int *widths, table_align_t def) {
    table_t *t = xcalloc(1, sizeof(*t));
    t->ncols = ncols;
    t->cols = xcalloc(ncols, sizeof(struct table_col));
    for (size_t i = 0; i < ncols; i++) {
        t->cols[i].width = (widths ? widths[i] : 0);
        t->cols[i].align = def;
    }
    t->default_align = def;
    t->cap_rows = 16;
    t->rows = xcalloc(t->cap_rows, sizeof(struct table_row));
    t->actual_widths = xcalloc(ncols, sizeof(int));
    return t;
}

void table_add_row(table_t *t, const char **cells) {
    if (!t || !cells) return;
    if (t->nrows >= t->cap_rows) {
        t->cap_rows *= 2;
        t->rows = xrealloc(t->rows, t->cap_rows * sizeof(struct table_row));
    }
    struct table_row *r = &t->rows[t->nrows++];
    r->cells = xcalloc(t->ncols, sizeof(char *));
    r->is_separator = 0;
    for (size_t i = 0; i < t->ncols; i++) {
        const char *c = cells[i] ? cells[i] : "";
        r->cells[i] = xstrdup(c);
        int len = (int)strlen(c);
        if (len > t->actual_widths[i]) t->actual_widths[i] = len;
    }
}

void table_add_separator(table_t *t) {
    if (!t) return;
    if (t->nrows >= t->cap_rows) {
        t->cap_rows *= 2;
        t->rows = xrealloc(t->rows, t->cap_rows * sizeof(struct table_row));
    }
    struct table_row *r = &t->rows[t->nrows++];
    r->cells = NULL;
    r->is_separator = 1;
}

static int detect_term_width(int hint) {
    if (hint > 0) return hint;
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) {
        return (int)w.ws_col;
    }
    return 80;
}

static void render_separator(FILE *fp, int total) {
    for (int i = 0; i < total; i++) fputc('-', fp);
    fputc('\n', fp);
}

static int truncate_to_width(const char *s, int width, char *buf) {
    int n = 0;
    while (*s && n < width) {
        /* UTF-8 简化：单字节字符保留；多字节按 2 字符算 */
        unsigned char c = (unsigned char)*s;
        int bytes = 1;
        if (c >= 0xc0) bytes = 2;
        if (c >= 0xe0) bytes = 3;
        if (c >= 0xf0) bytes = 4;
        if (n + bytes > width) break;
        for (int i = 0; i < bytes && *s; i++) buf[n++] = *s++;
    }
    if (*s) {
        /* 截断：附加 '…' */
        if (n + 3 <= width) {
            buf[n++] = '.';
            buf[n++] = '.';
            buf[n++] = '.';
        }
    }
    buf[n] = '\0';
    return n;
}

void table_render(table_t *t, FILE *fp, int term_width) {
    if (!t || t->nrows == 0) return;
    term_width = detect_term_width(term_width);

    /* 简化：列宽 = max(actual_width, default)，总和 ≤ term_width */
    int total = 0;
    for (size_t i = 0; i < t->ncols; i++) {
        int w = t->actual_widths[i] > 6 ? t->actual_widths[i] : 6;
        if (t->cols[i].width > 0) w = t->cols[i].width;
        t->actual_widths[i] = w;
        total += w + 3;  /* " " 分隔 */
    }
    total -= 3;  /* 末尾无分隔 */
    if (total > term_width) total = term_width;  /* 简单截断 */

    /* 渲染 */
    for (size_t r = 0; r < t->nrows; r++) {
        struct table_row *row = &t->rows[r];
        if (row->is_separator) {
            render_separator(fp, total);
            continue;
        }
        int col = 0;
        for (size_t i = 0; i < t->ncols; i++) {
            char buf[128];
            int w = t->actual_widths[i];
            truncate_to_width(row->cells[i], w, buf);
            int len = (int)strlen(buf);
            if (i > 0) fputs(" | ", fp);
            /* 对齐 */
            if (t->cols[i].align == TABLE_ALIGN_RIGHT) {
                for (int p = 0; p < w - len; p++) fputc(' ', fp);
                fputs(buf, fp);
            } else {
                fputs(buf, fp);
            }
            col += len;
        }
        fputc('\n', fp);
    }
}

void table_free(table_t *t) {
    if (!t) return;
    for (size_t r = 0; r < t->nrows; r++) {
        if (t->rows[r].cells) {
            for (size_t i = 0; i < t->ncols; i++) {
                free(t->rows[r].cells[i]);
            }
            free(t->rows[r].cells);
        }
    }
    free(t->rows);
    free(t->cols);
    free(t->actual_widths);
    free(t);
}
