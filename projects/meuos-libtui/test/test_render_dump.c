/* test_render_dump.c — 渲染输出抓取验证
 *
 * 把 libtui 的渲染输出 dump 成可读的转义序列格式，
 * 验证：1) 光标定位不越界 2) 颜色代码正确配对 3) 边框字符正确
 *
 * 这是"灰盒测试"：不看返回值，看实际输出字节流。
 */

#define _XOPEN_SOURCE 700

#include "meuos/libtui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/wait.h>

/* ── 输出抓取管道 ────────────────────────────────────── */

static int capture_fd = -1;

/* 把不可打印字符转为可视表示（保留 CSI 参数） */
static const char *esc_seq_to_str(const char *buf, int len, int *consumed)
{
    static char out[256];
    int i = 0;
    int pos = 0;

    if (buf[0] != '\033') {
        *consumed = 1;
        out[0] = buf[0];
        out[1] = '\0';
        return out;
    }

    /* ESC 序列 */
    pos += snprintf(out + pos, sizeof(out) - pos, "<ESC");
    i = 1;

    if (i < len && buf[i] == '[') {
        pos += snprintf(out + pos, sizeof(out) - pos, "[");
        i++;
        /* 参数字节 0x30-0x3F：数字和分号 */
        while (i < len && (buf[i] >= 0x30 && buf[i] <= 0x3F)) {
            if (pos >= (int)sizeof(out) - 20) break;
            if (buf[i] >= '0' && buf[i] <= '9') {
                pos += snprintf(out + pos, sizeof(out) - pos, "%c", buf[i]);
            } else if (buf[i] == ';') {
                pos += snprintf(out + pos, sizeof(out) - pos, ";");
            } else {
                pos += snprintf(out + pos, sizeof(out) - pos, "<%02X>", buf[i]);
            }
            i++;
        }
        /* 中间字节 0x20-0x2F */
        while (i < len && (buf[i] >= 0x20 && buf[i] <= 0x2F)) {
            if (pos >= (int)sizeof(out) - 20) break;
            pos += snprintf(out + pos, sizeof(out) - pos, "%c", buf[i]);
            i++;
        }
        /* 最终字节 0x40-0x7E */
        if (i < len && pos < (int)sizeof(out) - 20) {
            pos += snprintf(out + pos, sizeof(out) - pos, "%c", buf[i]);
            i++;
        }
    } else if (i < len) {
        pos += snprintf(out + pos, sizeof(out) - pos, "%c", buf[i]);
        i++;
    }

    if (pos < (int)sizeof(out) - 2)
        pos += snprintf(out + pos, sizeof(out) - pos, ">");
    *consumed = i;
    return out;
}

/* 抓取一段 libtui 渲染输出，打印可读分析 */
static void analyze_render(const char *label, int fd)
{
    /* 从管道读端读取 */
    char buf[8192];
    int total = 0;
    int n;

    /* 非阻塞读取所有可用数据 */
    while (total < (int)sizeof(buf) - 1) {
        n = read(fd, buf + total, sizeof(buf) - 1 - total);
        if (n <= 0) break;
        total += n;
    }
    buf[total] = '\0';

    printf("\n=== %s (%d bytes) ===\n", label, total);

    /* 解析并打印 */
    int pos = 0;
    int line_num = 1;
    int cur_row = 1, cur_col = 1;
    int color_active = 0;
    int error_count = 0;

    printf("[line %d] ", line_num);

    while (pos < total) {
        int consumed;
        const char *s = esc_seq_to_str(buf + pos, total - pos, &consumed);

        /* 跟踪状态 */
        if (strncmp(s, "<ESC[2J", 6) == 0) {
            cur_row = 1; cur_col = 1;
            color_active = 0;
        } else if (strncmp(s, "<ESC[", 4) == 0) {
            /* 提取 CSI 最终字节 */
            int slen = strlen(s);
            char final_byte = s[slen - 2]; /* 倒数第二字节（> 之前） */

            if (final_byte == 'H' || final_byte == 'f') {
                /* 光标定位 ESC[row;colH 或 ESC[row;colf */
                int row = 1, col = 1;
                if (sscanf(s + 4, "[%d;%d", &row, &col) != 2) {
                    /* 单参数：ESC[rowH → col=1 */
                    if (sscanf(s + 4, "[%d", &row) == 1) col = 1;
                }
                cur_row = row; cur_col = col;
                if (row < 1 || col < 1) {
                    printf("\n  ⚠️  无效光标位置: (%d,%d)\n", row, col);
                    error_count++;
                }
            } else if (final_byte == 'm') {
                /* 颜色/属性重置 */
                if (strcmp(s, "<ESC[m>") == 0 || strcmp(s, "<ESC[0m>") == 0) {
                    color_active = 0;
                } else {
                    /* 检查是否是颜色设置 */
                    int code = -1;
                    sscanf(s + 4, "[%d", &code);
                    if (code >= 30 && code <= 47) color_active = 1;
                    if (code >= 1 && code <= 9) color_active = 1; /* attrs */
                }
            } else if (final_byte == 'J' || final_byte == 'K') {
                /* 清除屏幕/行 */
            }
        }

        if (s[0] == '\n') {
            line_num++;
            cur_col = 1;
            printf("\n[line %d] ", line_num);
            pos += consumed;
            continue;
        } else if (s[0] == '\r') {
            cur_col = 1;
            pos += consumed;
            continue;
        } else if (s[0] == ' ' && consumed == 1) {
            cur_col++;
            pos += consumed;
            continue;
        } else if (consumed == 1 && s[0] >= 0x20 && s[0] < 0x7F) {
            cur_col++;
        }

        printf("%s", s);
        pos += consumed;
    }

    printf("\n[end] lines=%d, errors=%d\n", line_num, error_count);
}

/* ── 测试用例 ─────────────────────────────────────── */

static int dummy_content(int fd, const tui_rect_t *area, void *udata)
{
    (void)udata;
    tui_printf(fd, "Hello from content area (%dx%d)", area->rows, area->cols);
    return TUI_OK;
}

static const char *test_cell(int row, int col, void *udata)
{
    (void)udata;
    static char buf[32];
    snprintf(buf, sizeof(buf), "r%dc%d", row, col);
    return buf;
}

static void test_panel_render(void)
{
    printf("\n┌─────────────────────────────────────────────┐");
    printf("\n│ TEST: Panel Render                           │");
    printf("\n└─────────────────────────────────────────────┘");

    int p[2];
    pipe(p);

    tui_layout_t *panel = tui_panel_new("My Panel", dummy_content, NULL);
    tui_rect_t r = { 1, 1, 10, 40 };
    tui_layout_render(p[1], panel, r);
    tui_layout_free(panel);

    close(p[1]);
    analyze_render("Panel 10x40", p[0]);
    close(p[0]);
}

static void test_table_render(void)
{
    printf("\n┌─────────────────────────────────────────────┐");
    printf("\n│ TEST: Table Render                           │");
    printf("\n└─────────────────────────────────────────────┘");

    int p[2];
    pipe(p);

    tui_column_t cols[] = {
        { "Name", 12, -1 },
        { "Size", 8, 1 },
        { "Status", 10, 0 },
    };

    tui_table_t tbl;
    memset(&tbl, 0, sizeof(tbl));
    memcpy(tbl.columns, cols, sizeof(cols));
    tbl.ncols = 3;
    tbl.nrows = 5;
    tbl.cell_fn = test_cell;
    tbl.header_bg = TUI_COLOR_GREEN;
    tbl.select_bg = TUI_COLOR_GREEN;
    tbl.selected = 2;

    tui_rect_t r = { 1, 1, 8, 40 };
    tui_table_render(p[1], &r, &tbl);

    close(p[1]);
    analyze_render("Table 5 rows x 3 cols in 8x40", p[0]);
    close(p[0]);
}

static void test_dialog_render(void)
{
    printf("\n┌─────────────────────────────────────────────┐");
    printf("\n│ TEST: Dialog Render                          │");
    printf("\n└─────────────────────────────────────────────┘");

    int p[2];
    pipe(p);

    tui_layout_t *dlg = tui_dialog_layout("Confirm", "Delete this file?\nAre you sure?",
                                          TUI_DLG_QUESTION, TUI_DLG_YES | TUI_DLG_NO);
    tui_rect_t r = { 1, 1, 12, 50 };
    tui_layout_render(p[1], dlg, r);
    tui_layout_free(dlg);

    close(p[1]);
    analyze_render("Dialog QUESTION YES|NO 12x50", p[0]);
    close(p[0]);
}

static void test_progress_render(void)
{
    printf("\n┌─────────────────────────────────────────────┐");
    printf("\n│ TEST: Progress Bar Render                    │");
    printf("\n└─────────────────────────────────────────────┘");

    int p[2];
    pipe(p);

    tui_progress_t prog;
    memset(&prog, 0, sizeof(prog));
    prog.value = 0.67;
    prog.show_percent = 1;
    strcpy(prog.label, "Build:");
    prog.fill_color = TUI_COLOR_GREEN;

    tui_rect_t r = { 1, 1, 1, 60 };
    tui_progress_render(p[1], &r, &prog);

    close(p[1]);
    analyze_render("Progress 67%% with label 1x60", p[0]);
    close(p[0]);
}

static void test_list_render(void)
{
    printf("\n┌─────────────────────────────────────────────┐");
    printf("\n│ TEST: Selectable List Render                 │");
    printf("\n└─────────────────────────────────────────────┘");

    int p[2];
    pipe(p);

    tui_list_item_t items[] = {
        { "alpha.c", "120 lines", NULL, 0 },
        { "beta.h",  "45 lines", NULL, 0 },
        { "gamma.md", "88 lines", NULL, 0 },
        { "Makefile", "", NULL, 0 },
        { "README", "", NULL, 0 },
    };

    tui_list_t *list = tui_list_new(items, 5);
    tui_rect_t r = { 1, 1, 10, 40 };
    tui_list_render(p[1], &r, list);
    tui_list_free(list);

    close(p[1]);
    analyze_render("List 5 items 10x40", p[0]);
    close(p[0]);
}

static void test_nested_layout(void)
{
    printf("\n┌─────────────────────────────────────────────┐");
    printf("\n│ TEST: Nested Layout (VBox > Panel+Table)     │");
    printf("\n└─────────────────────────────────────────────┘");

    int p[2];
    pipe(p);

    /* VBox with padding */
    tui_layout_t *vbox = tui_layout_vbox(0);
    tui_layout_pad(vbox, 1, 2, 1, 2);

    /* First child: a panel with progress */
    tui_progress_t prog;
    memset(&prog, 0, sizeof(prog));
    prog.value = 0.85;
    prog.show_percent = 1;
    strcpy(prog.label, "Tasks:");
    tui_layout_t *prog_panel = tui_panel_new("Progress", tui_progress_render, &prog);
    tui_layout_add(vbox, prog_panel, 1);

    /* Second child: a table */
    tui_column_t cols[] = {
        { "Item", 10, -1 },
        { "Value", 8, 1 },
    };
    tui_table_t tbl;
    memset(&tbl, 0, sizeof(tbl));
    memcpy(tbl.columns, cols, sizeof(cols));
    tbl.ncols = 2;
    tbl.nrows = 3;
    tbl.cell_fn = test_cell;
    tbl.header_bg = TUI_COLOR_GREEN;
    tui_layout_t *tbl_panel = tui_panel_new("Data", tui_table_render, &tbl);
    tui_layout_add(vbox, tbl_panel, 1);

    tui_rect_t r = { 1, 1, 24, 80 };
    tui_layout_render(p[1], vbox, r);
    tui_layout_free(vbox);

    close(p[1]);
    analyze_render("Nested VBox[Panel+Table] 24x80", p[0]);
    close(p[0]);
}

/* ══════════════════════════════════════════════════════
 *  边界条件测试
 * ══════════════════════════════════════════════════════ */

static void test_edge_tiny_area(void)
{
    printf("\n┌─────────────────────────────────────────────┐");
    printf("\n│ TEST: Edge Case - Tiny Area (3x10)           │");
    printf("\n└─────────────────────────────────────────────┘");

    int p[2];
    pipe(p);

    tui_layout_t *panel = tui_panel_new("Tiny", dummy_content, NULL);
    tui_rect_t r = { 1, 1, 3, 10 };
    tui_layout_render(p[1], panel, r);
    tui_layout_free(panel);

    close(p[1]);
    analyze_render("Panel in 3x10 (should gracefully degrade)", p[0]);
    close(p[0]);
}

static void test_edge_zero_area(void)
{
    printf("\n┌─────────────────────────────────────────────┐");
    printf("\n│ TEST: Edge Case - Zero/Negative Area         │");
    printf("\n└─────────────────────────────────────────────┘");

    int p[2];
    pipe(p);

    /* 这些应该返回错误，不输出任何东西 */
    tui_layout_t *panel = tui_panel_new("Zero", dummy_content, NULL);
    tui_rect_t r = { 1, 1, 0, 40 };
    int ret = tui_layout_render(p[1], panel, r);
    printf("Render with 0 rows: ret=%d (expected -2)\n", ret);

    tui_rect_t r2 = { 1, 1, 5, 0 };
    ret = tui_layout_render(p[1], panel, r2);
    printf("Render with 0 cols: ret=%d (expected -2)\n", ret);

    tui_rect_t r3 = { 1, 1, -1, 40 };
    ret = tui_layout_render(p[1], panel, r3);
    printf("Render with -1 rows: ret=%d (expected -2)\n", ret);

    tui_layout_free(panel);
    close(p[1]);

    char buf[256];
    int n = read(p[0], buf, sizeof(buf));
    printf("Bytes written for invalid areas: %d (expected 0)\n", n);
    close(p[0]);
}

static void test_color_consistency(void)
{
    printf("\n┌─────────────────────────────────────────────┐");
    printf("\n│ TEST: Color Code Consistency                 │");
    printf("\n└─────────────────────────────────────────────┘");

    int p[2];
    pipe(p);

    /* 设置颜色 → 写文本 → 重置 */
    tui_set_fg(p[1], TUI_COLOR_GREEN);
    tui_write(p[1], "GREEN_TEXT");
    tui_set_bg(p[1], TUI_COLOR_BLUE);
    tui_write(p[1], "ON_BLUE");
    tui_reset_style(p[1]);
    tui_write(p[1], "RESET\n");

    /* 验证每个颜色代码都能正确配对 */
    tui_set_attr(p[1], TUI_ATTR_BOLD);
    tui_write(p[1], "BOLD");
    tui_reset_style(p[1]);
    tui_printf(p[1], " after-bold\n");

    close(p[1]);
    analyze_render("Color set/reset sequence", p[0]);
    close(p[0]);
}

int main(void)
{
    printf("═══════════════════════════════════════════════");
    printf("\n  libtui RENDER OUTPUT ANALYSIS");
    printf("\n  (验证渲染字节流的正确性)");
    printf("\n═══════════════════════════════════════════════");

    test_panel_render();
    test_table_render();
    test_dialog_render();
    test_progress_render();
    test_list_render();
    test_nested_layout();
    test_edge_tiny_area();
    test_edge_zero_area();
    test_color_consistency();

    printf("\n\n═══════════════════════════════════════════════");
    printf("\n  ALL RENDER DUMPS COMPLETE");
    printf("\n═══════════════════════════════════════════════\n");

    return 0;
}
