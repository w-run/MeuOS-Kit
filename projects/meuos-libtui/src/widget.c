/* widget.c — 可复用 TUI 组件
 *
 * 面板、进度条、旋转器、状态栏、文本样式、填充矩形等。
 * 纯 C11 + POSIX 实现，零外部依赖。
 */

#define _XOPEN_SOURCE 700

#include "meuos/libtui.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>

/* ══════════════════════════════════════════════════════
 *  默认主题
 * ══════════════════════════════════════════════════════ */

const tui_palette_t tui_meuos_theme = {
    .accent    = TUI_COLOR_GREEN,
    .bg        = TUI_COLOR_DEFAULT,
    .fg        = TUI_COLOR_DEFAULT,
    .border    = TUI_COLOR_GREEN,
    .highlight = TUI_COLOR_GREEN,
    .dim       = TUI_COLOR_BLACK,
    .success   = TUI_COLOR_GREEN,
    .warning   = TUI_COLOR_YELLOW,
    .error     = TUI_COLOR_RED,
    .info      = TUI_COLOR_CYAN,
};

/* ══════════════════════════════════════════════════════
 *  tui_rect_t
 * ══════════════════════════════════════════════════════ */

int tui_rect_valid(const tui_rect_t *r)
{
    return r && r->rows > 0 && r->cols > 0;
}

/* ══════════════════════════════════════════════════════
 *  辅助输出
 * ══════════════════════════════════════════════════════ */

int tui_spaces(int fd, int n)
{
    static const char spaces[64] =
        "                                                               ";
    while (n > 0) {
        int chunk = n > 64 ? 64 : n;
        if (write(fd, spaces, (size_t)chunk) != chunk) return TUI_ERR_IO;
        n -= chunk;
    }
    return TUI_OK;
}

int tui_hline(int fd, int col, int width, char ch, tui_color_t color)
{
    if (width <= 0) return TUI_OK;

    char buf[256];
    int  max = width < 255 ? width : 255;
    memset(buf, ch, (size_t)max);
    buf[max] = '\0';

    /* 如果指定颜色则设置 */
    if (color != TUI_COLOR_DEFAULT) {
        char esc[16];
        int n = snprintf(esc, sizeof(esc), "\033[%dm", 30 + (int)color);
        if (write(fd, esc, (size_t)n) != n) return TUI_ERR_IO;
    }

    if (write(fd, buf, (size_t)max) != max) return TUI_ERR_IO;

    if (color != TUI_COLOR_DEFAULT)
        if (write(fd, "\033[0m", 4) != 4) return TUI_ERR_IO;

    (void)col;
    return TUI_OK;
}

int tui_cprintf(int fd, tui_color_t fg, tui_color_t bg, const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    int n;

    /* 设置颜色 */
    char esc_fg[16], esc_bg[16];
    int len_fg = snprintf(esc_fg, sizeof(esc_fg), "\033[%dm", 30 + (int)fg);
    int len_bg = snprintf(esc_bg, sizeof(esc_bg), "\033[%dm", 40 + (int)bg);

    if (fg != TUI_COLOR_DEFAULT)
        write(fd, esc_fg, (size_t)len_fg);
    if (bg != TUI_COLOR_DEFAULT)
        write(fd, esc_bg, (size_t)len_bg);

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0) return TUI_ERR_PARAM;
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
    if (write(fd, buf, (size_t)n) != n) return TUI_ERR_IO;

    /* 重置 */
    if (write(fd, "\033[0m", 4) != 4) return TUI_ERR_IO;

    return TUI_OK;
}

/* ══════════════════════════════════════════════════════
 *  填充矩形
 * ══════════════════════════════════════════════════════ */

int tui_fill_rect(int fd, tui_rect_t area, tui_color_t bg)
{
    int r;
    char esc[16];
    int esc_n = snprintf(esc, sizeof(esc), "\033[%dm", 40 + (int)bg);

    if (esc_n <= 0) return TUI_ERR_PARAM;

    for (r = 0; r < area.rows; r++) {
        tui_cursor_goto(fd, area.row + r, area.col);
        if (write(fd, esc, (size_t)esc_n) != esc_n) return TUI_ERR_IO;
        if (tui_spaces(fd, area.cols) != TUI_OK) return TUI_ERR_IO;
    }

    tui_reset_style(fd);
    return TUI_OK;
}

int tui_styled_text(int fd, tui_rect_t area, const char *text, tui_style_t style)
{
    int len = (int)strlen(text);
    if (len > area.cols) len = area.cols;

    if (len <= 0) return TUI_OK;

    tui_cursor_goto(fd, area.row, area.col);
    tui_set_fg(fd, style.fg);
    tui_set_bg(fd, style.bg);
    if (style.attr) tui_set_attr(fd, style.attr);

    if (write(fd, text, (size_t)len) != len) return TUI_ERR_IO;

    /* 填充剩余空间 */
    if (len < area.cols)
        tui_spaces(fd, area.cols - len);

    tui_reset_style(fd);
    return TUI_OK;
}

/* ══════════════════════════════════════════════════════
 *  边框绘制 (面板)
 * ══════════════════════════════════════════════════════ */

/* 边框字符集 */
static const char *box_chars[4][6] = {
    /* 单线 */ { "─", "│", "┌", "┐", "└", "┘" },
    /* 双线 */ { "═", "║", "╔", "╗", "╚", "╝" },
    /* 圆角 */ { "─", "│", "╭", "╮", "╰", "╯" },
    /* 粗  */ { "━", "┃", "┏", "┓", "┗", "┛" },
};

/* UTF-8 字符串长度（字节数） */
#define BCLEN 3  /* 所有框线字符均为 3 字节 UTF-8 */

/* 绘制边框（公开 API，用于面板/对话框） */
int tui_draw_border(int fd, tui_rect_t *inner,
                    const char *title, int style_idx,
                    tui_color_t border_color)
{
    const char **box = box_chars[style_idx & 3];
    int r, left = 0;

    if (inner->rows < 3 || inner->cols < 4) return TUI_OK;

    /* 背景填充 */
    tui_set_bg(fd, TUI_COLOR_DEFAULT);

    /* 设置边框颜色 */
    char esc[16];
    snprintf(esc, sizeof(esc), "\033[%dm", 30 + (int)border_color);
    write(fd, esc, strlen(esc));

    tui_set_attr(fd, TUI_ATTR_BOLD);

    /* ── 上边框 ── */
    tui_cursor_goto(fd, inner->row, inner->col);
    write(fd, box[2], BCLEN);                  /* ┌/╔/╭/┏ */

    if (title && title[0]) {
        write(fd, " ", 1);
        tui_set_attr(fd, TUI_ATTR_RESET);
        tui_set_fg(fd, border_color);
        write(fd, title, strlen(title));
        tui_set_attr(fd, TUI_ATTR_BOLD);
        left = 2 + (int)strlen(title);
    }

    /* 上边框水平线：手动写入 UTF-8 字符循环 */
    {
        int i, hw = inner->cols - left - 1;
        tui_cursor_goto(fd, inner->row, inner->col + left);
        snprintf(esc, sizeof(esc), "\033[%dm", 30 + (int)border_color);
        write(fd, esc, strlen(esc));
        tui_set_attr(fd, TUI_ATTR_BOLD);
        for (i = 0; i < hw; i++)
            write(fd, box[0], BCLEN);
    }

    /* 行尾 ┐/╗/╮/┓ */
    tui_cursor_goto(fd, inner->row, inner->col + inner->cols - 1);
    write(fd, box[3], BCLEN);

    /* ── 下边框 ── */
    tui_cursor_goto(fd, inner->row + inner->rows - 1, inner->col);
    write(fd, box[4], BCLEN);                  /* └/╚/╰/┗ */

    {
        int i, hw = inner->cols - 2;
        tui_cursor_goto(fd, inner->row + inner->rows - 1, inner->col + 1);
        for (i = 0; i < hw; i++)
            write(fd, box[0], BCLEN);
    }

    tui_cursor_goto(fd, inner->row + inner->rows - 1, inner->col + inner->cols - 1);
    write(fd, box[5], BCLEN);                  /* ┘/╝/╯/┛ */

    /* ── 竖边框 ── */
    for (r = 1; r < inner->rows - 1; r++) {
        tui_cursor_goto(fd, inner->row + r, inner->col);
        write(fd, box[1], BCLEN);
        tui_cursor_goto(fd, inner->row + r, inner->col + inner->cols - 1);
        write(fd, box[1], BCLEN);
    }

    tui_reset_style(fd);

    /* 更新 inner 为内容区域 */
    inner->row    += 1;
    inner->rows   -= 2;
    inner->col    += 1;
    inner->cols   -= 2;

    return TUI_OK;
}

/* ── 面板渲染回调 ─────────────────────────────────── */

static int panel_render_fn(int fd, const tui_rect_t *area, void *userdata)
{
    tui_panel_t *panel = (tui_panel_t *)userdata;
    if (!panel) return TUI_ERR_PARAM;

    tui_rect_t r = *area;

    tui_draw_border(fd, &r, panel->title,
                    panel->border_style, panel->border_color);

    if (r.rows > 0 && r.cols > 0 && panel->content_fn)
        panel->content_fn(fd, &r, panel->content_data);

    return TUI_OK;
}

tui_layout_t *tui_panel_new(const char *title, tui_render_fn fn, void *data)
{
    tui_panel_t *p = (tui_panel_t *)calloc(1, sizeof(tui_panel_t));
    if (!p) return NULL;

    p->border_style = 0;      /* 单线 */
    p->border_color = tui_meuos_theme.border;
    p->title_color  = tui_meuos_theme.accent;
    p->content_fn   = fn;
    p->content_data = data;

    if (title)
        strncpy(p->title, title, sizeof(p->title) - 1);

    return tui_layout_leaf(panel_render_fn, p);
}

tui_layout_t *tui_panel_new_styled(const tui_panel_t *cfg)
{
    if (!cfg) return NULL;

    tui_panel_t *p = (tui_panel_t *)calloc(1, sizeof(tui_panel_t));
    if (!p) return NULL;

    *p = *cfg;
    return tui_layout_leaf(panel_render_fn, p);
}

void tui_panel_set_style(tui_panel_t *panel, const tui_panel_t *cfg)
{
    if (!panel || !cfg) return;
    if (cfg->border_style) panel->border_style = cfg->border_style;
    if (cfg->border_color) panel->border_color = cfg->border_color;
    if (cfg->title_color)  panel->title_color  = cfg->title_color;
}

/* ══════════════════════════════════════════════════════
 *  进度条
 * ══════════════════════════════════════════════════════ */

int tui_progress_render(int fd, const tui_rect_t *area, void *userdata)
{
    tui_progress_t *p = (tui_progress_t *)userdata;
    if (!p || !area) return TUI_ERR_PARAM;

    int bar_w = p->bar_width > 0 ? p->bar_width : area->cols - 2;
    if (bar_w < 4) bar_w = 4;

    int filled = (int)(p->value * bar_w);
    if (filled < 0)   filled = 0;
    if (filled > bar_w) filled = bar_w;

    tui_cursor_goto(fd, area->row, area->col);

    /* 标签 */
    if (p->label[0]) {
        tui_set_fg(fd, tui_meuos_theme.dim);
        tui_write(fd, p->label);
        tui_write(fd, " ");
        tui_reset_style(fd);
    }

    /* 进度条: [████░░░░] */
    tui_set_fg(fd, TUI_COLOR_DEFAULT);
    tui_write(fd, "[");

    /* 已填充部分 */
    if (filled > 0) {
        tui_color_t fc = p->fill_color ? p->fill_color : tui_meuos_theme.accent;
        tui_set_fg(fd, fc);
        tui_set_attr(fd, TUI_ATTR_BOLD);

        int i;
        for (i = 0; i < filled; i++) {
            /* 若已到最后填充格且不满整条，绘箭头 */
            if (i == filled - 1 && filled < bar_w)
                write(fd, ">", 1);
            else
                write(fd, "=", 1);
        }
    }

    /* 未填充部分 */
    if (filled < bar_w) {
        tui_reset_style(fd);
        tui_set_attr(fd, TUI_ATTR_DIM);
        int i;
        for (i = filled; i < bar_w; i++)
            write(fd, "-", 1);
    }

    tui_reset_style(fd);
    tui_write(fd, "]");

    /* 百分比 */
    if (p->show_percent) {
        int pct = (int)(p->value * 100.0);
        if (pct > 100) pct = 100;
        tui_set_fg(fd, tui_meuos_theme.dim);
        tui_printf(fd, " %3d%%", pct);
    }

    tui_reset_style(fd);
    return TUI_OK;
}

/* ══════════════════════════════════════════════════════
 *  旋转器
 * ══════════════════════════════════════════════════════ */

int tui_spinner_render(int fd, const tui_rect_t *area, void *userdata)
{
    tui_spinner_t *s = (tui_spinner_t *)userdata;
    if (!s || !area) return TUI_ERR_PARAM;

    tui_cursor_goto(fd, area->row, area->col);

    int nframes = (int)strlen(s->frames) / 3;
    if (nframes == 0) {
        const char *def = TUI_SPINNER_FRAMES;
        int ndef = (int)strlen(def) / 3;
        int idx = (s->frame % (ndef > 0 ? ndef : 1)) * 3;
        tui_set_fg(fd, s->color ? s->color : tui_meuos_theme.accent);
        write(fd, def + idx, 3);
    } else {
        int idx = (s->frame % nframes) * 3;
        tui_set_fg(fd, s->color ? s->color : tui_meuos_theme.accent);
        write(fd, s->frames + idx, 3);
    }

    tui_reset_style(fd);
    return TUI_OK;
}

void tui_spinner_tick(tui_spinner_t *s)
{
    if (s) s->frame++;
}

/* ══════════════════════════════════════════════════════
 *  状态栏
 * ══════════════════════════════════════════════════════ */

int tui_statusbar_render(int fd, const tui_rect_t *area, void *userdata)
{
    tui_statusbar_t *sb = (tui_statusbar_t *)userdata;
    if (!sb || !area) return TUI_ERR_PARAM;

    tui_cursor_goto(fd, area->row, area->col);

    tui_color_t bg = sb->bg ? sb->bg : tui_meuos_theme.accent;
    tui_color_t fg = sb->fg ? sb->fg : TUI_COLOR_DEFAULT;

    tui_set_bg(fd, bg);
    tui_set_fg(fd, fg);
    tui_set_attr(fd, TUI_ATTR_BOLD);

    int left_len = (int)strlen(sb->left);
    int right_len = (int)strlen(sb->right);
    int max_left = area->cols - right_len - 1;
    if (max_left < 0) max_left = 0;

    if (left_len > 0) {
        int show = left_len < max_left ? left_len : max_left;
        write(fd, sb->left, (size_t)show);
    }

    int pad = area->cols - (left_len < max_left ? left_len : max_left) - right_len;
    if (pad > 0) tui_spaces(fd, pad);

    if (right_len > 0)
        write(fd, sb->right, (size_t)right_len);

    tui_reset_style(fd);
    return TUI_OK;
}

/* ══════════════════════════════════════════════════════
 *  模板快捷函数
 * ══════════════════════════════════════════════════════ */

typedef struct {
    char text[64];
} header_data_t;

static int header_render(int fd, const tui_rect_t *area, void *userdata)
{
    header_data_t *h = (header_data_t *)userdata;
    if (!h || !area) return TUI_ERR_PARAM;

    tui_cursor_goto(fd, area->row, area->col);

    tui_set_bg(fd, tui_meuos_theme.accent);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_spaces(fd, area->cols);

    tui_cursor_goto(fd, area->row, area->col + 2);
    tui_set_fg(fd, TUI_COLOR_WHITE);
    tui_set_bg(fd, tui_meuos_theme.accent);

    int len = (int)strlen(h->text);
    int max = area->cols - 4;
    if (len > max) len = max;
    write(fd, h->text, (size_t)len);

    tui_reset_style(fd);
    return TUI_OK;
}

tui_layout_t *tui_app_layout(const char *header,
                             tui_render_fn content_fn, void *content_data,
                             const char *status_left, const char *status_right)
{
    tui_layout_t *root = tui_layout_vbox(1);
    if (!root) return NULL;

    if (header && header[0]) {
        header_data_t *hd = (header_data_t *)calloc(1, sizeof(header_data_t));
        if (!hd) { tui_layout_free(root); return NULL; }
        strncpy(hd->text, header, sizeof(hd->text) - 1);

        tui_layout_t *hdr = tui_layout_leaf(header_render, hd);
        if (!hdr) { free(hd); tui_layout_free(root); return NULL; }
        tui_layout_add(root, hdr, 0);
    }

    tui_layout_t *content = tui_layout_leaf(content_fn, content_data);
    if (!content) { tui_layout_free(root); return NULL; }
    tui_layout_add(root, content, 1);

    if (status_left || status_right) {
        tui_statusbar_t *sb = (tui_statusbar_t *)calloc(1, sizeof(tui_statusbar_t));
        if (!sb) { tui_layout_free(root); return NULL; }
        if (status_left)  strncpy(sb->left,  status_left,  sizeof(sb->left) - 1);
        if (status_right) strncpy(sb->right, status_right, sizeof(sb->right) - 1);
        sb->bg = tui_meuos_theme.accent;
        sb->fg = TUI_COLOR_WHITE;

        tui_layout_t *st = tui_layout_leaf(tui_statusbar_render, sb);
        if (!st) { free(sb); tui_layout_free(root); return NULL; }
        tui_layout_add(root, st, 0);
    }

    return root;
}

tui_layout_t *tui_split_layout(int sidebar_width,
                               tui_render_fn side_fn, void *side_data,
                               tui_render_fn content_fn, void *content_data)
{
    tui_layout_t *root = tui_layout_hbox(0);
    if (!root) return NULL;

    tui_layout_t *side = tui_layout_leaf(side_fn, side_data);
    if (!side) { tui_layout_free(root); return NULL; }
    tui_layout_add(root, side, 0);

    tui_layout_t *content = tui_layout_leaf(content_fn, content_data);
    if (!content) { tui_layout_free(root); return NULL; }
    tui_layout_add(root, content, 1);

    (void)sidebar_width;
    return root;
}

/* ══════════════════════════════════════════════════════
 *  Label 标签
 * ══════════════════════════════════════════════════════ */

int tui_label_render(int fd, const tui_rect_t *area, void *userdata)
{
    tui_label_t *lb = (tui_label_t *)userdata;
    if (!lb || !area) return TUI_ERR_PARAM;
    if (!tui_rect_valid(area)) return TUI_OK;

    int len = (int)strlen(lb->text);
    int pad = area->cols - len;
    if (pad < 0) pad = 0;

    tui_cursor_goto(fd, area->row, area->col);

    /* 对齐 */
    if (lb->align == 0)        tui_spaces(fd, pad / 2);
    else if (lb->align > 0)    tui_spaces(fd, pad);

    tui_set_fg(fd, lb->fg);
    tui_set_bg(fd, lb->bg);
    if (lb->attr) tui_set_attr(fd, lb->attr);

    int show = len < area->cols ? len : area->cols;
    write(fd, lb->text, (size_t)show);

    tui_reset_style(fd);
    return TUI_OK;
}

tui_layout_t *tui_label_new(const char *text, tui_color_t fg)
{
    tui_label_t *lb = (tui_label_t *)calloc(1, sizeof(tui_label_t));
    if (!lb) return NULL;
    if (text) strncpy(lb->text, text, sizeof(lb->text) - 1);
    lb->fg = fg;
    lb->bg = TUI_COLOR_DEFAULT;
    return tui_layout_leaf(tui_label_render, lb);
}

tui_layout_t *tui_heading(const char *text, tui_color_t fg)
{
    tui_label_t *lb = (tui_label_t *)calloc(1, sizeof(tui_label_t));
    if (!lb) return NULL;
    if (text) strncpy(lb->text, text, sizeof(lb->text) - 1);
    lb->fg   = fg;
    lb->bg   = TUI_COLOR_DEFAULT;
    lb->attr = TUI_ATTR_BOLD;
    return tui_layout_leaf(tui_label_render, lb);
}

/* ══════════════════════════════════════════════════════
 *  Separator 分隔线
 * ══════════════════════════════════════════════════════ */

int tui_separator_render(int fd, const tui_rect_t *area, void *userdata)
{
    tui_separator_t *sep = (tui_separator_t *)userdata;
    if (!sep || !area) return TUI_ERR_PARAM;
    if (!tui_rect_valid(area)) return TUI_OK;

    char ch = sep->line_char ? sep->line_char : '-';

    if (sep->vertical) {
        tui_cursor_goto(fd, area->row, area->col);
        tui_set_fg(fd, sep->color);
        int r;
        for (r = 0; r < area->rows; r++) {
            tui_cursor_goto(fd, area->row + r, area->col);
            write(fd, &ch, 1);
        }
    } else {
        int label_len = sep->label[0] ? (int)strlen(sep->label) + 2 : 0;
        int left = (area->cols - label_len) / 2;
        if (left < 0) left = 0;
        int right = area->cols - left - label_len;
        if (right < 0) right = 0;

        tui_cursor_goto(fd, area->row, area->col);
        tui_set_fg(fd, sep->color);
        tui_set_attr(fd, TUI_ATTR_DIM);

        int i;
        for (i = 0; i < left; i++) write(fd, &ch, 1);
        if (sep->label[0]) {
            tui_reset_style(fd);
            tui_set_fg(fd, sep->color);
            tui_write(fd, " ");
            tui_write(fd, sep->label);
            tui_write(fd, " ");
            tui_set_fg(fd, sep->color);
            tui_set_attr(fd, TUI_ATTR_DIM);
        }
        for (i = 0; i < right; i++) write(fd, &ch, 1);

        tui_reset_style(fd);
    }

    return TUI_OK;
}

tui_layout_t *tui_hr(tui_color_t color)
{
    return tui_hr_label(NULL, color);
}

tui_layout_t *tui_hr_label(const char *label, tui_color_t color)
{
    tui_separator_t *sep = (tui_separator_t *)calloc(1, sizeof(tui_separator_t));
    if (!sep) return NULL;
    sep->line_char = '-';
    sep->color     = color ? color : tui_meuos_theme.dim;
    if (label) strncpy(sep->label, label, sizeof(sep->label) - 1);
    return tui_layout_leaf(tui_separator_render, sep);
}

/* ══════════════════════════════════════════════════════
 *  Badge 徽章
 * ══════════════════════════════════════════════════════ */

int tui_badge_render(int fd, const tui_rect_t *area, void *userdata)
{
    tui_badge_t *b = (tui_badge_t *)userdata;
    if (!b || !area) return TUI_ERR_PARAM;
    if (!tui_rect_valid(area)) return TUI_OK;

    int len = (int)strlen(b->text);
    int pad_l = (area->cols - len) / 2;
    int pad_r = area->cols - len - pad_l;
    if (pad_l < 1) pad_l = 1;
    if (pad_r < 1) pad_r = 1;

    tui_cursor_goto(fd, area->row, area->col);
    tui_set_fg(fd, b->fg ? b->fg : TUI_COLOR_WHITE);
    tui_set_bg(fd, b->bg);
    tui_set_attr(fd, TUI_ATTR_BOLD);

    tui_spaces(fd, pad_l);
    write(fd, b->text, (size_t)(len < area->cols ? len : area->cols));
    tui_spaces(fd, pad_r);

    tui_reset_style(fd);
    return TUI_OK;
}

tui_layout_t *tui_badge_new(const char *text, tui_color_t bg)
{
    tui_badge_t *b = (tui_badge_t *)calloc(1, sizeof(tui_badge_t));
    if (!b) return NULL;
    if (text) strncpy(b->text, text, sizeof(b->text) - 1);
    b->bg = bg;
    b->fg = TUI_COLOR_WHITE;
    return tui_layout_leaf(tui_badge_render, b);
}

/* ══════════════════════════════════════════════════════
 *  Table 表格
 * ══════════════════════════════════════════════════════ */

int tui_table_render(int fd, const tui_rect_t *area, void *userdata)
{
    tui_table_t *t = (tui_table_t *)userdata;
    if (!t || !area) return TUI_ERR_PARAM;
    if (!tui_rect_valid(area)) return TUI_OK;

    int y = area->row;
    int ncols = t->ncols;
    int nrows = t->nrows;

    if (ncols <= 0) return TUI_OK;
    if (ncols > TUI_TABLE_MAX_COLS) ncols = TUI_TABLE_MAX_COLS;
    if (nrows > TUI_TABLE_MAX_ROWS) nrows = TUI_TABLE_MAX_ROWS;

    /* 总行数不能超过可用行数 */
    int max_rows = area->rows;
    if (max_rows <= 0) return TUI_OK;

    /* ── 表头 ── */
    tui_cursor_goto(fd, y, area->col);
    tui_color_t hbg = t->header_bg ? t->header_bg : tui_meuos_theme.accent;
    tui_set_bg(fd, hbg);
    tui_set_attr(fd, TUI_ATTR_BOLD);

    int col_x[TUI_TABLE_MAX_COLS];
    int x = area->col;
    int c;
    for (c = 0; c < ncols; c++) {
        col_x[c] = x;
        int w = t->columns[c].width;
        tui_cursor_goto(fd, y, x);

        const char *hdr = t->columns[c].header;
        int hlen = (int)strlen(hdr);
        int pad = (w - hlen) / 2;
        if (pad < 0) pad = 0;

        tui_spaces(fd, pad);
        write(fd, hdr, (size_t)(hlen < w ? hlen : w));
        if (w - pad - hlen > 0)
            tui_spaces(fd, w - pad - hlen);

        x += w + 1; /* +1 为列间距 */
        if (x >= area->col + area->cols) break;
    }
    tui_reset_style(fd);

    /* ── 分隔线 ── */
    y++;
    if (y >= area->row + max_rows) return TUI_OK;
    tui_cursor_goto(fd, y, area->col);
    tui_set_attr(fd, TUI_ATTR_DIM);
    tui_set_fg(fd, tui_meuos_theme.dim);
    int i;
    for (i = 0; i < area->cols && area->col + i < x; i++)
        write(fd, "─", 1);
    tui_reset_style(fd);

    /* ── 数据行 ── */
    for (int ri = 0; ri < nrows && y + 1 < area->row + max_rows; ri++) {
        y++;
        int is_sel = (ri == t->selected);

        for (c = 0; c < ncols; c++) {
            if (col_x[c] >= area->col + area->cols) break;

            tui_cursor_goto(fd, y, col_x[c]);
            int w = t->columns[c].width;

            if (is_sel) {
                tui_set_bg(fd, t->select_bg ? t->select_bg : tui_meuos_theme.highlight);
                tui_set_fg(fd, TUI_COLOR_WHITE);
            }

            const char *cell = t->cell_fn ? t->cell_fn(ri, c, t->userdata) : "";
            if (!cell) cell = "";
            int clen = (int)strlen(cell);
            int align = t->columns[c].align;

            int pad_l = 0;
            if (align == 0)      pad_l = (w - clen) / 2;
            else if (align > 0)  pad_l = w - clen;
            if (pad_l < 0) pad_l = 0;

            int show = clen < w ? clen : w;
            tui_spaces(fd, pad_l);
            write(fd, cell, (size_t)show);
            if (w - pad_l - show > 0)
                tui_spaces(fd, w - pad_l - show);

            if (is_sel) tui_reset_style(fd);

            /* 列间距 */
            if (c < ncols - 1) {
                if (is_sel) {
                    tui_set_bg(fd, t->select_bg ? t->select_bg : tui_meuos_theme.highlight);
                }
                tui_spaces(fd, 1);
                if (is_sel) tui_reset_style(fd);
            }
        }

        if (is_sel) {
            /* 填充行尾 */
            int fill = area->col + area->cols - col_x[ncols - 1] -
                       t->columns[ncols - 1].width;
            if (fill > 0) {
                tui_set_bg(fd, t->select_bg ? t->select_bg : tui_meuos_theme.highlight);
                tui_spaces(fd, fill);
                tui_reset_style(fd);
            }
        }
    }

    return TUI_OK;
}

int tui_table_handle(tui_table_t *t, tui_event_t *ev)
{
    if (!t || !ev) return TUI_ERR_PARAM;

    switch (ev->key) {
    case TUI_KEY_UP:
        if (t->selected > 0) t->selected--;
        break;
    case TUI_KEY_DOWN:
        if (t->selected < t->nrows - 1) t->selected++;
        break;
    case TUI_KEY_HOME:
        t->selected = 0;
        break;
    case TUI_KEY_END:
        t->selected = t->nrows - 1;
        break;
    default:
        return 0;  /* 未处理 */
    }
    return 1;  /* 已处理 */
}
