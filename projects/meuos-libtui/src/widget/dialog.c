/* dialog.c — 对话框组件
 *
 * 消息框/确认框/警告/错误/输入对话框。
 * 支持多种按钮组合、键盘导航（←→ 切换按钮，Enter 确认）。
 * 适合安装器、包管理器、设置工具、AI Agent 交互等场景。
 * 纯 C11 + POSIX 实现，零外部依赖。
 */

#define _XOPEN_SOURCE 700

#include "meuos/libtui.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

/* ── 类型图标 ─────────────────────────────────────── */

static const char *type_icon(tui_dlg_type_t type)
{
    switch (type) {
    case TUI_DLG_INFO:     return " ℹ ";   /* info */
    case TUI_DLG_WARNING:  return " ⚠ ";   /* warning */
    case TUI_DLG_ERROR:    return " ✗ ";   /* error */
    case TUI_DLG_QUESTION: return " ? ";   /* question */
    case TUI_DLG_INPUT:    return " ⌨ ";   /* input */
    default:               return "   ";
    }
}

static tui_color_t type_color(tui_dlg_type_t type)
{
    switch (type) {
    case TUI_DLG_INFO:     return tui_meuos_theme.info;
    case TUI_DLG_WARNING:  return tui_meuos_theme.warning;
    case TUI_DLG_ERROR:    return tui_meuos_theme.error;
    case TUI_DLG_QUESTION: return tui_meuos_theme.accent;
    case TUI_DLG_INPUT:    return tui_meuos_theme.info;
    default:               return tui_meuos_theme.fg;
    }
}

/* ── 按钮字符串辅助 ───────────────────────────────── */

typedef struct {
    int   button;
    const char *label;
} btn_entry_t;

static btn_entry_t all_buttons[] = {
    { TUI_DLG_OK,     " OK "     },
    { TUI_DLG_CANCEL, " Cancel " },
    { TUI_DLG_YES,    " Yes "    },
    { TUI_DLG_NO,     " No "     },
    { TUI_DLG_RETRY,  " Retry "  },
    { TUI_DLG_ABORT,  " Abort "  },
    { TUI_DLG_IGNORE, " Ignore " },
    { 0, NULL }
};

/* ── 统计按钮数量 ─────────────────────────────────── */

static int count_buttons(int btn_mask)
{
    int n = 0;
    for (int i = 0; all_buttons[i].label; i++)
        if (btn_mask & all_buttons[i].button) n++;
    return n;
}

static int btn_index_to_mask(int btn_mask, int idx)
{
    int n = 0;
    for (int i = 0; all_buttons[i].label; i++) {
        if (btn_mask & all_buttons[i].button) {
            if (n == idx) return all_buttons[i].button;
            n++;
        }
    }
    return 0;
}

static int btn_mask_to_index(int btn_mask, int target)
{
    int n = 0;
    for (int i = 0; all_buttons[i].label; i++) {
        if (btn_mask & all_buttons[i].button) {
            if (all_buttons[i].button == target) return n;
            n++;
        }
    }
    return 0;
}

/* ── 默认选中按钮 ─────────────────────────────────── */

static int default_selected_btn(int buttons)
{
    if (buttons & TUI_DLG_OK)     return TUI_DLG_OK;
    if (buttons & TUI_DLG_YES)    return TUI_DLG_YES;
    if (buttons & TUI_DLG_RETRY)  return TUI_DLG_RETRY;
    if (buttons & TUI_DLG_CANCEL) return TUI_DLG_CANCEL;
    if (buttons & TUI_DLG_NO)     return TUI_DLG_NO;
    if (buttons & TUI_DLG_ABORT)  return TUI_DLG_ABORT;
    if (buttons & TUI_DLG_IGNORE) return TUI_DLG_IGNORE;
    return 0;
}

/* ══════════════════════════════════════════════════════
 *  渲染
 * ══════════════════════════════════════════════════════ */

int tui_dialog_render(int fd, const tui_rect_t *area, void *userdata)
{
    tui_dialog_t *dlg = (tui_dialog_t *)userdata;
    if (!dlg || !area) return TUI_ERR_PARAM;
    if (!tui_rect_valid(area)) return TUI_OK;

    tui_rect_t inner = *area;

    /* ── 边框 ── */
    tui_color_t col = type_color(dlg->type);
    tui_draw_border(fd, &inner, dlg->title, 2, col);

    if (!tui_rect_valid(&inner)) { tui_reset_style(fd); return TUI_OK; }

    int y = inner.row;

    /* ── 图标行 ── */
    tui_cursor_goto(fd, y, inner.col);
    tui_set_fg(fd, col);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_write(fd, type_icon(dlg->type));
    tui_reset_style(fd);
    y++;

    /* ── 消息文本（居中显示，自动换行处理）── */
    if (y >= inner.row + inner.rows) { tui_reset_style(fd); return TUI_OK; }

    const char *msg = dlg->message;
    int line_w = inner.cols - 4;
    if (line_w < 1) line_w = 1;

    /* 按宽度换行（正确处理多字节字符） */
    const char *cur = msg;
    while (*cur && y < inner.row + inner.rows - 2) {
        int bytes = tui_truncate(cur, line_w);
        if (bytes <= 0) break;

        tui_cursor_goto(fd, y, inner.col + 2);
        tui_set_fg(fd, TUI_COLOR_DEFAULT);
        write(fd, cur, (size_t)bytes);
        tui_reset_style(fd);

        cur += bytes;
        /* 跳过换行符 */
        if (*cur == '\n') cur++;
        y++;
    }

    /* ── 输入框（INPUT 类型） ── */
    if (dlg->type == TUI_DLG_INPUT && y < inner.row + inner.rows - 2) {
        y++;
        tui_cursor_goto(fd, y, inner.col + 2);

        int input_w = inner.cols - 6;
        if (input_w < 4) input_w = 4;

        /* 输入框背景 */
        tui_set_bg(fd, TUI_COLOR_BLACK);
        tui_spaces(fd, input_w);

        tui_cursor_goto(fd, y, inner.col + 3);

        tui_set_attr(fd, TUI_ATTR_REVERSE);
        int dlg_input_len = (int)strlen(dlg->input);
        int dlg_show = dlg_input_len < input_w - 2 ? dlg_input_len : input_w - 2;
        write(fd, dlg->input, (size_t)dlg_show);
        tui_reset_style(fd);

        if (dlg->input_active) {
            /* cursor handled by reverse attr */
        }

        tui_reset_style(fd);
        y++;
    }

    /* ── 按钮行 ── */
    if (y < inner.row + inner.rows) {
        int nbtns = count_buttons(dlg->buttons);
        if (nbtns > 0) {
            /* 计算每个按钮宽度和总宽度 */
            int btn_ws[8], btn_idxs[8];
            int n = 0, total_w = 0;

            for (int i = 0; all_buttons[i].label && n < 8; i++) {
                if (!(dlg->buttons & all_buttons[i].button)) continue;
                btn_idxs[n] = i;
                btn_ws[n]   = tui_strwidth(all_buttons[i].label) + 4; /* " [Label] " */
                total_w += btn_ws[n];
                n++;
            }

            int gap = 4;  /* 按钮间距 */
            total_w += (n - 1) * gap;

            int start_x = inner.col + (inner.cols - total_w) / 2;
            if (start_x < inner.col) start_x = inner.col;

            tui_cursor_goto(fd, y, start_x);

            for (int b = 0; b < n; b++) {
                int i   = btn_idxs[b];
                int is_sel = (all_buttons[i].button == dlg->selected_btn);

                if (is_sel) {
                    /* 选中按钮：高亮背景 + 尖括号 */
                    tui_set_bg(fd, col);
                    tui_set_fg(fd, TUI_COLOR_WHITE);
                    tui_set_attr(fd, TUI_ATTR_BOLD);
                    tui_write(fd, " ");
                    tui_write(fd, "< ");
                    tui_write(fd, all_buttons[i].label);
                    tui_write(fd, " >");
                    tui_write(fd, " ");
                    tui_reset_style(fd);
                } else {
                    /* 非选中：普通方括号 */
                    tui_set_fg(fd, col);
                    tui_write(fd, " ");
                    tui_write(fd, "[");
                    tui_reset_style(fd);
                    tui_set_fg(fd, TUI_COLOR_DEFAULT);
                    tui_write(fd, all_buttons[i].label);
                    tui_set_fg(fd, col);
                    tui_write(fd, "]");
                    tui_reset_style(fd);
                    tui_write(fd, " ");
                }

                /* 按钮间距 */
                if (b < n - 1) {
                    tui_write(fd, "  ");
                }
            }
        }
    }

    tui_reset_style(fd);
    return TUI_OK;
}

/* ══════════════════════════════════════════════════════
 *  事件处理
 * ══════════════════════════════════════════════════════ */

int tui_dialog_handle(tui_dialog_t *dlg, tui_event_t *ev)
{
    if (!dlg || !ev) return TUI_ERR_PARAM;

    int nbtns = count_buttons(dlg->buttons);
    int cur_idx = btn_mask_to_index(dlg->buttons, dlg->selected_btn);

    if (dlg->type == TUI_DLG_INPUT && dlg->input_active) {
        /* ── 输入模式 ── */
        if (ev->key == TUI_KEY_CR || ev->key == TUI_KEY_LF) {
            dlg->input_active = 0;
            return 1;
        }
        if (ev->key == TUI_KEY_ESC) {
            dlg->input_active = 0;
            dlg->selected_btn = TUI_DLG_CANCEL;
            return 1;
        }
        if (ev->key == TUI_KEY_BS) {
            int len = (int)strlen(dlg->input);
            if (dlg->input_cursor > 0) {
                dlg->input_cursor--;
                memmove(dlg->input + dlg->input_cursor,
                        dlg->input + dlg->input_cursor + 1,
                        (size_t)(len - dlg->input_cursor));
                dlg->input[len - 1] = '\0';
            }
            return 1;
        }
        if (ev->key == TUI_KEY_LEFT && dlg->input_cursor > 0) {
            dlg->input_cursor--;
            return 1;
        }
        if (ev->key == TUI_KEY_RIGHT && dlg->input_cursor < (int)strlen(dlg->input)) {
            dlg->input_cursor++;
            return 1;
        }
        if (ev->key >= 0x20 && ev->key <= 0x7E) {
            int len = (int)strlen(dlg->input);
            if (len < (int)sizeof(dlg->input) - 1) {
                memmove(dlg->input + dlg->input_cursor + 1,
                        dlg->input + dlg->input_cursor,
                        (size_t)(len - dlg->input_cursor + 1));
                dlg->input[dlg->input_cursor] = (char)ev->key;
                dlg->input_cursor++;
            }
            return 1;
        }
        return 0;
    }

    /* ── 按钮选择模式 ── */
    switch (ev->key) {
    case TUI_KEY_LEFT:
        if (cur_idx > 0)
            dlg->selected_btn = btn_index_to_mask(dlg->buttons, cur_idx - 1);
        return 1;

    case TUI_KEY_RIGHT:
        if (cur_idx < nbtns - 1)
            dlg->selected_btn = btn_index_to_mask(dlg->buttons, cur_idx + 1);
        return 1;

    case TUI_KEY_TAB:
        if (ev->key == TUI_KEY_TAB && nbtns > 0)
            dlg->selected_btn = btn_index_to_mask(dlg->buttons,
                (cur_idx + 1) % nbtns);
        return 1;

    case TUI_KEY_CR:
    case TUI_KEY_LF:
        /* 确认按钮 */
        if (dlg->type == TUI_DLG_INPUT) {
            dlg->input_active = 1;
            return 1;
        }
        return 1;  /* caller 应检查 tui_dialog_result */

    case TUI_KEY_ESC:
        /* ESC → 取消/退出 */
        if (dlg->buttons & TUI_DLG_CANCEL)
            dlg->selected_btn = TUI_DLG_CANCEL;
        else if (dlg->buttons & TUI_DLG_NO)
            dlg->selected_btn = TUI_DLG_NO;
        return 1;

    default:
        return 0;
    }
}

/* ══════════════════════════════════════════════════════
 *  结果查询
 * ══════════════════════════════════════════════════════ */

int tui_dialog_result(tui_dialog_t *dlg)
{
    return dlg ? dlg->selected_btn : 0;
}

const char *tui_dialog_input(tui_dialog_t *dlg)
{
    return dlg ? dlg->input : "";
}

/* ══════════════════════════════════════════════════════
 *  Layout 叶子
 * ══════════════════════════════════════════════════════ */

tui_layout_t *tui_dialog_layout(const char *title, const char *message,
                                tui_dlg_type_t type, int buttons)
{
    tui_dialog_t *dlg = (tui_dialog_t *)calloc(1, sizeof(tui_dialog_t));
    if (!dlg) return NULL;

    dlg->type = type;
    dlg->buttons = buttons;
    dlg->selected_btn = default_selected_btn(buttons);
    dlg->input_active = (type == TUI_DLG_INPUT);
    dlg->input_cursor = 0;
    dlg->input[0] = '\0';

    if (title)   strncpy(dlg->title, title, sizeof(dlg->title) - 1);
    if (message) strncpy(dlg->message, message, sizeof(dlg->message) - 1);

    return tui_layout_leaf(tui_dialog_render, dlg);
}

/* ══════════════════════════════════════════════════════
 *  阻塞式简便对话框
 * ══════════════════════════════════════════════════════ */

int tui_dialog_blocking(int fd, tui_rect_t area, const char *title,
                        const char *message, tui_dlg_type_t type, int buttons)
{
    tui_dialog_t dlg;
    memset(&dlg, 0, sizeof(dlg));

    dlg.type = type;
    dlg.buttons = buttons;
    dlg.selected_btn = default_selected_btn(buttons);
    dlg.input_active = (type == TUI_DLG_INPUT);
    dlg.input_cursor = 0;
    dlg.input[0] = '\0';

    if (title)   strncpy(dlg.title, title, sizeof(dlg.title) - 1);
    if (message) strncpy(dlg.message, message, sizeof(dlg.message) - 1);

    /* 将 fd 设为原始模式用于读取按键 */
    tui_raw_mode(fd, 1);
    tui_cursor_show(fd, 0);

    /* 获取屏幕尺寸用于全屏遮罩 */
    tui_size_t scr;
    if (tui_get_size(fd, &scr) != TUI_OK) {
        scr.rows = 24;
        scr.cols = 80;
    }

    tui_event_t ev;
    int done = 0;
    int result = 0;

    while (!done) {
        /* ── 清屏并绘制全屏遮罩 ── */
        /* 使用默认背景色清屏，确保完全覆盖底层内容 */
        tui_cursor_goto(fd, 0, 0);
        tui_set_attr(fd, TUI_ATTR_RESET);

        int mr;
        for (mr = 0; mr < scr.rows; mr++) {
            tui_cursor_goto(fd, mr, 0);
            /* 先用空格填充整行（使用默认背景） */
            tui_spaces(fd, scr.cols);
        }

        /* 渲染对话框（在干净背景之上） */
        tui_dialog_render(fd, &area, &dlg);

        /* 等待按键 */
        if (tui_getkey(fd, &ev) == TUI_OK) {
            tui_dialog_handle(&dlg, &ev);

            /* 检查是否按下确认键 */
            if (ev.key == TUI_KEY_CR || ev.key == TUI_KEY_LF) {
                result = dlg.selected_btn;
                if (dlg.type != TUI_DLG_INPUT || !dlg.input_active)
                    done = 1;
            }
            if (ev.key == TUI_KEY_ESC) {
                result = dlg.selected_btn;
                done = 1;
            }
        }
    }

    tui_cursor_show(fd, 1);
    tui_raw_mode(fd, 0);

    return result;
}
