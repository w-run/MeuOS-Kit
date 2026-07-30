/* demo_list.c — 可选择列表 + 双栏布局演示
 *
 * 模拟一个包管理器场景：左侧是包列表，右侧显示选中包详情。
 * 键盘操作: ↑↓ 选择, Enter 确认, q 退出
 * 运行方式: make demo_list && ./build/demo_list
 */

#define _XOPEN_SOURCE 700

#include "meuos/libtui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* ── 包列表数据 ───────────────────────────────────── */

typedef struct {
    const char *name;
    const char *version;
    const char *status;
    const char *desc;
} package_t;

static package_t packages[] = {
    { "meuos-libtui", "1.0.0",  "installed", "Terminal UI library for MeuOS" },
    { "meuos-libc",   "0.9.2",  "installed", "C standard library for MeuOS" },
    { "mcc",          "0.8.5",  "installed", "MeuOS C cross compiler" },
    { "meow",         "0.7.1",  "upgradable","MeuOS kernel" },
    { "busybox",      "1.36.0", "installed", "Swiss army knife of embedded Linux" },
    { "dropbear",     "2024.84","available", "SSH server and client" },
    { "zlib",         "1.3",    "installed", "Compression library" },
    { "curl",         "8.9.0",  "available", "URL transfer library" },
    { "git",          "2.45.0", "available", "Version control system" },
    { "python3",      "3.12.0", "available", "Python programming language" },
    { "nodejs",       "22.0.0", "available", "JavaScript runtime" },
    { "vim",          "9.1.0",  "installed", "Text editor" },
};

#define NPACKAGES (sizeof(packages) / sizeof(packages[0]))

/* ── 将包数据转为列表项 ─────────────────────────────── */

static tui_list_item_t list_items[NPACKAGES];

static void init_items(void)
{
    for (size_t i = 0; i < NPACKAGES; i++) {
        strncpy(list_items[i].label, packages[i].name, sizeof(list_items[i].label) - 1);
        snprintf(list_items[i].secondary, sizeof(list_items[i].secondary),
                 "%s  %s", packages[i].version, packages[i].status);
        list_items[i].userdata = (void *)&packages[i];
    }
}

/* ── 详情面板渲染 ──────────────────────────────────── */

static int detail_render(int fd, const tui_rect_t *area, void *userdata)
{
    package_t *pkg = (package_t *)userdata;
    int y = area->row + 1;
    int x = area->col + 2;

    if (!pkg) {
        tui_cursor_goto(fd, y, x);
        tui_set_attr(fd, TUI_ATTR_DIM);
        tui_write(fd, "Select a package to view details");
        tui_reset_style(fd);
        return TUI_OK;
    }

    tui_cursor_goto(fd, y, x);
    tui_set_fg(fd, tui_meuos_theme.accent);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_write(fd, pkg->name);
    tui_reset_style(fd);

    y += 2;
    tui_cursor_goto(fd, y, x);
    tui_set_attr(fd, TUI_ATTR_DIM);
    tui_printf(fd, "Version: ");
    tui_reset_style(fd);
    tui_set_fg(fd, TUI_COLOR_WHITE);
    tui_printf(fd, "%s", pkg->version);
    tui_reset_style(fd);

    y++;
    tui_cursor_goto(fd, y, x);
    tui_set_attr(fd, TUI_ATTR_DIM);
    tui_printf(fd, "Status:  ");
    tui_reset_style(fd);

    tui_color_t st_col = tui_meuos_theme.success;
    if (strcmp(pkg->status, "available") == 0)
        st_col = tui_meuos_theme.info;
    else if (strcmp(pkg->status, "upgradable") == 0)
        st_col = tui_meuos_theme.warning;

    tui_set_fg(fd, st_col);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_printf(fd, "%s", pkg->status);
    tui_reset_style(fd);

    y += 2;
    tui_cursor_goto(fd, y, x);
    tui_set_attr(fd, TUI_ATTR_ITALIC);
    tui_write(fd, pkg->desc);
    tui_reset_style(fd);

    return TUI_OK;
}

/* ══════════════════════════════════════════════════════
 *  main
 * ══════════════════════════════════════════════════════ */

int main(void)
{
    init_items();

    tui_list_t *list = tui_list_new(list_items, (int)NPACKAGES);

    tui_raw_mode(0, 1);
    tui_alt_screen(0, 1);
    tui_cursor_show(0, 0);

    tui_size_t size;
    if (tui_get_size(0, &size) != TUI_OK) {
        size.rows = 24;
        size.cols = 80;
    }

    tui_event_t ev;
    int running = 1;

    while (running) {
        tui_clear_screen(0);

        /* ── 双栏布局渲染 ── */
        package_t *sel_pkg = (package_t *)tui_list_selected_data(list);

        tui_layout_t *dual = tui_layout_dual(
            28, "  Packages  ",
            tui_list_render, list,
            detail_render, sel_pkg
        );

        tui_rect_t full = { 3, 1, size.rows - 4, size.cols - 3 };
        tui_layout_render(0, dual, full);
        tui_layout_free(dual);

        /* ── Header ── */
        tui_cursor_goto(0, 1, 1);
        tui_set_bg(0, tui_meuos_theme.accent);
        tui_set_attr(0, TUI_ATTR_BOLD);
        tui_set_fg(0, TUI_COLOR_WHITE);
        tui_spaces(0, size.cols - 1);
        tui_cursor_goto(0, 1, 3);
        tui_write(0, "Package Manager - libtui Demo");
        tui_reset_style(0);

        /* ── Status bar ── */
        tui_cursor_goto(0, size.rows, 1);
        tui_set_bg(0, tui_meuos_theme.accent);
        tui_set_fg(0, TUI_COLOR_WHITE);
        tui_set_attr(0, TUI_ATTR_BOLD);
        tui_spaces(0, size.cols - 1);
        tui_cursor_goto(0, size.rows, 3);
        tui_printf(0, " %s  |  %d packages", sel_pkg ? sel_pkg->name : "", (int)NPACKAGES);
        tui_cursor_goto(0, size.rows, size.cols - 22);
        tui_printf(0, " %-16s", "q=quit  arrows=nav");
        tui_reset_style(0);

        /* ── 等待按键 ── */
        tui_getkey(0, &ev);

        if (ev.key == TUI_KEY_ESC ||
            (ev.key >= 0x20 && ev.key < 0x7F && (char)ev.key == 'q')) {
            running = 0;
        } else {
            tui_list_handle(list, &ev);
        }
    }

    tui_list_free(list);
    tui_cursor_show(0, 1);
    tui_alt_screen(0, 0);
    tui_raw_mode(0, 0);
    tui_clear_screen(0);

    return 0;
}
