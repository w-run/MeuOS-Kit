/* demo.c — meuos-libtui 综合仪表盘演示
 *
 * 展示：布局模板、面板表格、进度条、徽章、标签、分隔线。
 * 运行: make demo && ./build/demo
 */

#define _XOPEN_SOURCE 700

#include "meuos/libtui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* ══════════════════════════════════════════════════════
 *  表格数据
 * ══════════════════════════════════════════════════════ */

static const char *files_cb(int row, int col, void *udata)
{
    (void)udata;
    static const char *data[][4] = {
        { "main.c",        "12 K",  "07-30", "rw-r--r--" },
        { "libtui.h",      "8.2K",  "07-30", "rw-r--r--" },
        { "Makefile",      "512",   "07-29", "rw-r--r--" },
        { "README.md",     "2.1K",  "07-28", "rw-r--r--" },
        { "src/",          "---",   "07-30", "drwxr-xr-x" },
        { "build/",        "---",   "07-30", "drwxr-xr-x" },
    };
    if (row < 6 && col < 4)
        return data[row][col];
    return "";
}

/* ══════════════════════════════════════════════════════
 *  内容区渲染
 * ══════════════════════════════════════════════════════ */

static int demo_content(int fd, const tui_rect_t *area, void *udata)
{
    (void)udata;
    int y = area->row + 1;
    int x = area->col + 2;
    int w = area->cols - 4;

    if (w < 10) return TUI_OK;

    /* ── 装饰 Banner 标题 ── */
    tui_layout_t *banner = tui_banner_new("  MeuOS TUI  ",
                                           "Terminal UI Framework",
                                           tui_meuos_theme.accent);
    tui_rect_t br0 = { y, x, 3, w };
    tui_layout_render(fd, banner, br0);
    tui_layout_free(banner);
    y += 4;

    /* ── 分隔线 ── */
    tui_layout_t *sep = tui_hr_label("Overview", tui_meuos_theme.info);
    tui_rect_t sr = { y, x, 1, w };
    tui_layout_render(fd, sep, sr);
    tui_layout_free(sep);
    y += 2;

    /* ── 信息行：徽章 ── */
    tui_layout_t *b[3] = {
        tui_badge_new(" meuos-libtui ", TUI_COLOR_GREEN),
        tui_badge_new(" C11 ", 6),
        tui_badge_new(" v1.0.0 ", 3),
    };
    int bx = x, gap = 2;
    for (int i = 0; i < 3; i++) {
        tui_rect_t br = { y, bx, 1, 24 };
        if (b[i]) {
            tui_layout_render(fd, b[i], br);
            tui_layout_free(b[i]);
        }
        bx += br.cols + gap;
    }
    y += 2;

    /* ── 进度条 ── */
    tui_progress_t prog;
    memset(&prog, 0, sizeof(prog));
    prog.value = 0.67;
    prog.show_percent = 1;
    strcpy(prog.label, "Build:");
    prog.fill_color = TUI_COLOR_GREEN;
    tui_rect_t pr = { y, x, 1, w };
    tui_progress_render(fd, &pr, &prog);
    y += 2;

    /* ── 第二个分隔线 ── */
    tui_layout_t *sep2 = tui_hr(tui_meuos_theme.dim);
    tui_rect_t sr2 = { y, x, 1, w };
    tui_layout_render(fd, sep2, sr2);
    tui_layout_free(sep2);
    y += 2;

    /* ── 文件表格 ── */
    tui_column_t cols[4] = {
        { "Name",   14, -1 },
        { "Size",    6,  1 },
        { "Date",    8,  0 },
        { "Mode",   10, -1 },
    };

    static tui_table_t tbl;
    memset(&tbl, 0, sizeof(tbl));
    memcpy(tbl.columns, cols, sizeof(cols));
    tbl.ncols     = 4;
    tbl.nrows     = 6;
    tbl.cell_fn   = files_cb;
    tbl.header_bg = 2;
    tbl.select_bg = 2;
    tbl.selected  = 1;

    int tbl_h = 9;
    if (y + tbl_h <= area->row + area->rows) {
        tui_layout_t *panel = tui_panel_new("  Files  ",
                                             tui_table_render, &tbl);
        tui_rect_t pnr = { y, x, tbl_h, w };
        tui_layout_render(fd, panel, pnr);
        tui_layout_free(panel);
    }

    return TUI_OK;
}

/* ══════════════════════════════════════════════════════
 *  main
 * ══════════════════════════════════════════════════════ */

int main(void)
{
    tui_raw_mode(0, 1);
    tui_clear_screen(0);
    tui_cursor_show(0, 0);

    tui_size_t size;

    /* 容错：若无法获取尺寸则设默认值 */
    if (tui_get_size(0, &size) != TUI_OK) {
        size.rows = 24;
        size.cols = 80;
    }

    tui_layout_t *app = tui_app_layout(
        "  MeuOS Kit - libtui Demo  ",
        demo_content, NULL,
        " READY  |  press 'q' to quit ",
        " libtui v1.0 "
    );

    if (app) {
        tui_rect_t area = { 1, 1, size.rows, size.cols - 1 };
        tui_layout_render(0, app, area);
        tui_layout_free(app);
    }

    tui_event_t ev;
    do { tui_getkey(0, &ev); }
    while (ev.key != (tui_key_t)'q' && ev.key != TUI_KEY_ESC);

    tui_cursor_show(0, 1);
    tui_clear_screen(0);
    tui_raw_mode(0, 0);

    return 0;
}
