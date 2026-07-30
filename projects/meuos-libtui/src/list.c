/* list.c — 可选择列表组件
 *
 * 支持键盘导航（↑↓↖↘）、滚动、选择的列表。
 * 适合菜单、文件列表、包选择、命令历史等交互场景。
 * 纯 C11 + POSIX 实现，零外部依赖。
 */

#define _XOPEN_SOURCE 700

#include "meuos/libtui.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ── 列表结构 ─────────────────────────────────────── */

struct tui_list {
    tui_list_item_t *items;
    int              nitems;
    int              selected;      /* 当前选中索引 */
    int              scroll;        /* 顶部可见行 */
};

/* ── 创建与销毁 ───────────────────────────────────── */

tui_list_t *tui_list_new(tui_list_item_t *items, int nitems)
{
    tui_list_t *list = (tui_list_t *)calloc(1, sizeof(tui_list_t));
    if (!list) return NULL;

    list->items    = items;
    list->nitems   = nitems;
    list->selected = 0;
    list->scroll   = 0;

    return list;
}

void tui_list_free(tui_list_t *list)
{
    if (!list) return;
    /* items 由外部管理 */
    free(list);
}

/* ── 状态查询 ─────────────────────────────────────── */

int tui_list_selected(tui_list_t *list)
{
    return list ? list->selected : -1;
}

void *tui_list_selected_data(tui_list_t *list)
{
    if (!list || list->selected < 0 || list->selected >= list->nitems)
        return NULL;
    return list->items[list->selected].userdata;
}

/* ── 动态修改 ─────────────────────────────────────── */

void tui_list_select(tui_list_t *list, int idx)
{
    if (!list) return;
    if (idx < 0) idx = 0;
    if (idx >= list->nitems) idx = list->nitems - 1;
    list->selected = idx;
}

void tui_list_set_items(tui_list_t *list, tui_list_item_t *items, int nitems)
{
    if (!list) return;
    list->items  = items;
    list->nitems = nitems;
    if (list->selected >= nitems)
        list->selected = nitems > 0 ? nitems - 1 : 0;
}

/* ── 事件处理 ─────────────────────────────────────── */

int tui_list_handle(tui_list_t *list, tui_event_t *ev)
{
    if (!list || !ev) return TUI_ERR_PARAM;

    int n = list->nitems;
    if (n <= 0) return 0;

    switch (ev->key) {
    case TUI_KEY_UP:
        if (list->selected > 0) {
            list->selected--;
            if (list->selected < list->scroll)
                list->scroll = list->selected;
        }
        return 1;

    case TUI_KEY_DOWN:
        if (list->selected < n - 1) {
            list->selected++;
            /* 确保可见 */
        }
        return 1;

    case TUI_KEY_HOME:
        list->selected = 0;
        list->scroll   = 0;
        return 1;

    case TUI_KEY_END:
        list->selected = n - 1;
        return 1;

    default:
        return 0;  /* 未处理 */
    }
}

/* ── 渲染 ─────────────────────────────────────────── */

int tui_list_render(int fd, const tui_rect_t *area, void *userdata)
{
    tui_list_t *list = (tui_list_t *)userdata;
    if (!list || !area) return TUI_ERR_PARAM;
    if (!tui_rect_valid(area)) return TUI_OK;

    int n = list->nitems;
    if (n <= 0) {
        tui_cursor_goto(fd, area->row, area->col);
        tui_set_attr(fd, TUI_ATTR_DIM);
        tui_write(fd, "(empty)");
        tui_reset_style(fd);
        return TUI_OK;
    }

    /* 确保选中项可见 */
    if (list->selected < list->scroll)
        list->scroll = list->selected;
    if (list->selected >= list->scroll + area->rows)
        list->scroll = list->selected - area->rows + 1;
    if (list->scroll < 0) list->scroll = 0;

    int y = area->row;
    int max_y = area->row + area->rows;

    for (int i = list->scroll; i < n && y < max_y; i++, y++) {
        int is_sel = (i == list->selected);

        tui_cursor_goto(fd, y, area->col);

        if (is_sel) {
            /* 选中行：高亮背景 */
            tui_set_bg(fd, tui_meuos_theme.highlight);
            tui_set_fg(fd, TUI_COLOR_WHITE);
            tui_set_attr(fd, TUI_ATTR_BOLD);
        } else if (list->items[i].disabled) {
            tui_set_attr(fd, TUI_ATTR_DIM);
        }

        /* 图标/指示符 */
        if (is_sel) {
            tui_write(fd, "◆ ");
        } else {
            tui_write(fd, "  ");
        }

        /* 标签 */
        const char *label = list->items[i].label;
        int llen = (int)strlen(label);
        int max_txt = area->cols - 4;
        if (max_txt < 0) max_txt = 0;

        int show = llen < max_txt ? llen : max_txt;
        write(fd, label, (size_t)show);

        /* 副文本 */
        if (show > 0 && list->items[i].secondary[0]) {
            int remaining = area->cols - show - 4;
            int slen = (int)strlen(list->items[i].secondary);
            int ss = slen < remaining ? slen : remaining;
            if (ss > 0) {
                tui_reset_style(fd);
                tui_set_attr(fd, TUI_ATTR_DIM);
                tui_spaces(fd, remaining - ss);
                write(fd, list->items[i].secondary, (size_t)ss);
            }
        }

        /* 填充行尾 */
        tui_reset_style(fd);
        if (is_sel) {
            tui_set_bg(fd, tui_meuos_theme.highlight);
            tui_spaces(fd, area->cols - (area->col + area->cols));
            tui_reset_style(fd);
        }
    }

    /* 填充剩余行 */
    for (; y < max_y; y++) {
        tui_cursor_goto(fd, y, area->col);
        tui_clear_line(fd);
    }

    return TUI_OK;
}

/* ── Layout 叶子 ──────────────────────────────────── */

tui_layout_t *tui_list_layout(tui_list_t *list)
{
    return tui_layout_leaf(tui_list_render, list);
}
