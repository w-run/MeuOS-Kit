/* demo.c — meuos-libtui 综合演示
 *
 * 展示布局模板、面板、表格、进度条、徽章、标签、分隔线等组件。
 * 运行方式: make demo && ./build/demo
 */

#define _XOPEN_SOURCE 700

#include "meuos/libtui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* ══════════════════════════════════════════════════════
 *  表格数据回调
 * ══════════════════════════════════════════════════════ */

static const char *files_cb(int row, int col, void *udata)
{
    (void)udata;
    static const char *data[][4] = {
        { "main.c",        "12K",  "Jul 30", "rw-r--r--" },
        { "libtui.h",      "8.2K", "Jul 30", "rw-r--r--" },
        { "Makefile",      "512",  "Jul 29", "rw-r--r--" },
        { "README.md",     "2.1K", "Jul 28", "rw-r--r--" },
        { "src/",          "---",  "Jul 30", "drwxr-xr-x" },
        { "build/",        "---",  "Jul 30", "drwxr-xr-x" },
    };
    if (row < 6 && col < 4)
        return data[row][col];
    return "";
}

/* ══════════════════════════════════════════════════════
 *  内容区回调 (demo 主渲染)
 * ══════════════════════════════════════════════════════ */

static int demo_content(int fd, const tui_rect_t *area, void *udata)
{
    (void)udata;
    int y = area->row;

    /* ── 标题区 ── */
    tui_layout_t *heading = tui_heading("  Welcome to MeuOS TUI  ",
                                        tui_meuos_theme.accent);
    tui_rect_t hr = { y, area->col, 1, area->cols };
    tui_layout_render(fd, heading, hr);
    tui_layout_free(heading);
    y += 2;

    /* ── 分隔线 ── */
    tui_layout_t *sep = tui_hr_label("Project Overview", tui_meuos_theme.info);
    tui_rect_t sr = { y, area->col, 1, area->cols };
    tui_layout_render(fd, sep, sr);
    tui_layout_free(sep);
    y += 2;

    /* ── 信息行：徽章 ── */
    tui_cursor_goto(fd, y, area->col);

    tui_layout_t *b1 = tui_badge_new("meuos-libtui", tui_meuos_theme.accent);
    tui_rect_t b1r = { y, area->col, 1, 16 };
    tui_layout_render(fd, b1, b1r);
    tui_layout_free(b1);

    tui_layout_t *b2 = tui_badge_new("C11", tui_meuos_theme.info);
    tui_rect_t b2r = { y, area->col + 18, 1, 8 };
    tui_layout_render(fd, b2, b2r);
    tui_layout_free(b2);

    tui_layout_t *b3 = tui_badge_new("v1.0.0", tui_meuos_theme.warning);
    tui_rect_t b3r = { y, area->col + 28, 1, 12 };
    tui_layout_render(fd, b3, b3r);
    tui_layout_free(b3);
    y += 2;

    /* ── 进度条 ── */
    tui_progress_t prog;
    memset(&prog, 0, sizeof(prog));
    prog.value = 0.67;
    prog.show_percent = 1;
    strcpy(prog.label, "Build:");
    prog.fill_color = tui_meuos_theme.accent;
    tui_rect_t pr = { y, area->col, 1, area->cols };
    tui_progress_render(fd, &pr, &prog);
    y += 2;

    /* ── 分隔线 ── */
    tui_layout_t *sep2 = tui_hr(tui_meuos_theme.dim);
    tui_rect_t sr2 = { y, area->col, 1, area->cols };
    tui_layout_render(fd, sep2, sr2);
    tui_layout_free(sep2);
    y += 2;

    /* ── 文件表格 ── */
    tui_column_t cols[4] = {
        { "Name",   14, -1 },
        { "Size",    8,  1 },
        { "Date",   10,  0 },
        { "Mode",   12, -1 },
    };

    static tui_table_t tbl;
    memset(&tbl, 0, sizeof(tbl));
    memcpy(tbl.columns, cols, sizeof(cols));
    tbl.ncols     = 4;
    tbl.nrows     = 6;
    tbl.cell_fn   = files_cb;
    tbl.header_bg = tui_meuos_theme.accent;
    tbl.select_bg = tui_meuos_theme.accent;
    tbl.selected  = 1;

    /* 表格外面包一个 panel */
    tui_layout_t *panel = tui_panel_new("  Files  ",
                                         tui_table_render, &tbl);
    tui_rect_t pnr = { y, area->col, 9, area->cols };
    tui_layout_render(fd, panel, pnr);
    tui_layout_free(panel);

    return TUI_OK;
}

/* ══════════════════════════════════════════════════════
 *  main
 * ══════════════════════════════════════════════════════ */

int main(void)
{
    /* 进入原始模式和备用屏幕 */
    tui_raw_mode(0, 1);
    tui_alt_screen(0, 1);
    tui_cursor_show(0, 0);

    /* 获取终端大小 */
    tui_size_t size;
    tui_get_size(0, &size);

    /* 创建并渲染应用布局 */
    tui_layout_t *app = tui_app_layout(
        "  MeuOS Kit - libtui Demo  ",
        demo_content, NULL,
        " READY  |  press 'q' to quit ",
        " libtui v1.0 "
    );

    tui_rect_t area = { 1, 1, size.rows, size.cols };
    tui_clear_screen(0);
    tui_layout_render(0, app, area);

    /* 等待按键退出 */
    tui_event_t ev;
    do {
        tui_getkey(0, &ev);
    } while (ev.key != 'q' && ev.key != TUI_KEY_ESC);

    /* 清理 */
    tui_layout_free(app);
    tui_cursor_show(0, 1);
    tui_alt_screen(0, 0);
    tui_raw_mode(0, 0);
    tui_clear_screen(0);

    return 0;
}
