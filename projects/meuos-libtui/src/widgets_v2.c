/* widgets_v2.c — meuos-libtui 第二代 widgets
 *
 * Tabs / KeyHints / Stat / Sparkline / Card
 * + Banner/Progress/Spinner/Badge v2 风格
 *
 * 全部基于 tui_theme_current() 读取颜色；支持 24-bit 真彩色。
 */

#define _XOPEN_SOURCE 700

#include "meuos/libtui.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

/* Forward declarations for v2 render callbacks (used by factory functions
 * before their definitions appear below). */
int tui_banner_render_v2(int fd, const tui_rect_t *area, void *userdata);
static int tui_progress_render_v2(int fd, const tui_rect_t *area, void *userdata);
static int tui_spinner_render_v2(int fd, const tui_rect_t *area, void *userdata);
static int tui_badge_render_v2(int fd, const tui_rect_t *area, void *userdata);

/* ── helpers ────────────────────────────────────────── */

static tui_rgb_t color_or_theme_fg(tui_color_t c)
{
    if (c == TUI_COLOR_DEFAULT) {
        const tui_theme_t *th = tui_theme_current();
        return th ? th->surface_fg : (tui_rgb_t){229,229,229};
    }
    return tui_color_to_rgb_xterm(c);
}

static tui_rgb_t color_or_theme(tui_color_t c, tui_rgb_t fallback)
{
    if (c == TUI_COLOR_DEFAULT || c == 0) return fallback;
    return tui_color_to_rgb_xterm(c);
}

/* 取主题渐变色索引 */
static tui_rgb_t theme_gradient(int idx)
{
    const tui_theme_t *th = tui_theme_current();
    if (!th || !th->use_24bit) return (tui_rgb_t){200,200,200};
    int n = (int)(sizeof(th->gradient) / sizeof(th->gradient[0]));
    if (n <= 0) return (tui_rgb_t){200,200,200};
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    return th->gradient[idx];
}

/* ══════════════════════════════════════════════════════
 *  Tab 标签栏
 * ══════════════════════════════════════════════════════ */

int tui_tabbar_render(int fd, const tui_rect_t *area, void *userdata)
{
    tui_tabbar_t *tb = (tui_tabbar_t *)userdata;
    if (!tb || !area) return TUI_ERR_PARAM;
    if (!tui_rect_valid(area)) return TUI_OK;
    if (tb->ntabs <= 0) return TUI_OK;

    const tui_theme_t *th = tui_theme_current();
    tui_rgb_t accent = color_or_theme(tb->accent, th ? th->gradient[4] : (tui_rgb_t){74,222,128});
    tui_rgb_t dim    = (tui_rgb_t){ 90,  90, 100};
    tui_rgb_t surf   = th ? th->surface_bg : (tui_rgb_t){ 13, 13, 23};

    /* 底色：清空本行 */
    tui_cursor_goto(fd, area->row, area->col);
    tui_set_bg_rgb(fd, surf);
    tui_spaces(fd, area->cols);
    tui_reset_style(fd);

    int x = area->col + 1;
    int max_x = area->col + area->cols - 1;
    for (int i = 0; i < tb->ntabs; i++) {
        tui_tab_t *t = &tb->tabs[i];
        if (!t->label) continue;

        int is_active = (i == tb->selected) || t->active;
        if (i == tb->selected) tb->tabs[i].active = 1;
        is_active = tb->tabs[i].active;

        /* tab 宽度 = 文本宽 + 2 padding + badge */
        int txt_w = tui_strwidth(t->label);
        int badge_w = t->badge ? (tui_strwidth(t->badge) + 2) : 0;
        int tab_w = txt_w + badge_w + 4;  /* 1 空格 + 文本 + badge + 1 空格 + 2 边距 */
        if (x + tab_w > max_x) break;

        if (is_active) {
            /* 选中：彩色背景条 + 加粗 */
            tui_cursor_goto(fd, area->row, x);
            tui_set_fg_rgb(fd, surf);
            tui_set_bg_rgb(fd, accent);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            tui_write(fd, " ");
            tui_write(fd, t->label);
            if (t->badge) {
                tui_write(fd, " ");
                /* badge 颜色：反白更醒目 */
                tui_set_fg_rgb(fd, accent);
                tui_set_bg_rgb(fd, surf);
                tui_write(fd, t->badge);
                tui_set_fg_rgb(fd, surf);
                tui_set_bg_rgb(fd, accent);
            }
            tui_write(fd, " ");
        } else {
            tui_cursor_goto(fd, area->row, x);
            tui_set_bg_rgb(fd, surf);
            tui_set_fg_rgb(fd, dim);
            tui_set_attr(fd, TUI_ATTR_DIM);
            tui_write(fd, " ");
            tui_write(fd, t->label);
            if (t->badge) {
                tui_write(fd, " ");
                tui_write(fd, t->badge);
            }
            tui_write(fd, " ");
        }
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, surf);

        /* tab 分隔符 */
        if (x + tab_w + 1 < max_x) {
            tui_cursor_goto(fd, area->row, x + tab_w);
            tui_set_bg_rgb(fd, surf);
            tui_set_fg_rgb(fd, dim);
            tui_write(fd, "│");
        }

        x += tab_w + 1;
    }

    /* 行底装饰横线 */
    tui_cursor_goto(fd, area->row, area->col + area->cols - 1);
    tui_set_bg_rgb(fd, surf);
    tui_set_fg_rgb(fd, dim);
    tui_write(fd, " ");
    tui_reset_style(fd);

    return TUI_OK;
}

tui_layout_t *tui_tabbar_new(tui_tab_t *tabs, int ntabs, int selected)
{
    tui_tabbar_t *tb = (tui_tabbar_t *)calloc(1, sizeof(tui_tabbar_t));
    if (!tb) return NULL;
    tb->tabs = tabs;
    tb->ntabs = ntabs;
    tb->selected = selected;
    tb->accent = TUI_COLOR_DEFAULT;
    return tui_layout_leaf(tui_tabbar_render, tb);
}

/* ══════════════════════════════════════════════════════
 *  ModernKeyBar 现代化底部状态栏
 *
 *  设计目标：脱离 nano/tmux 老式硬朗感，向 Linear / Raycast / Arc
 *  那种现代化、扁平、克制的交互栏看齐。
 *
 *  v4 设计（可见且克制）：
 *   - 顶部 1px 细线（dim border）
 *   - key 渲染为 subtle chip：淡色背景 + accent 粗体文字
 *     背景 = bg * 0.80 + accent * 0.20，足够轻但又清晰可见
 *   - label 紧接 chip，fg 颜色，无背景（减少视觉噪音）
 *   - 分组之间用单点 · + 2 空格分隔
 *   - 右侧 dim dot + theme + accent time
 * ══════════════════════════════════════════════════════ */

int tui_keyhints_render(int fd, const tui_rect_t *area, void *userdata)
{
    tui_keyhints_t *kh = (tui_keyhints_t *)userdata;
    if (!kh || !area) return TUI_ERR_PARAM;
    if (!tui_rect_valid(area)) return TUI_OK;
    if (kh->nhints <= 0) return TUI_OK;

    const tui_theme_t *th = tui_theme_current();
    tui_rgb_t bg     = th ? th->surface_bg : (tui_rgb_t){13, 13, 23};
    tui_rgb_t fg     = th ? th->surface_fg : (tui_rgb_t){220, 222, 230};
    tui_rgb_t dim    = th ? tui_color_to_rgb_xterm(th->palette.dim) : (tui_rgb_t){110, 115, 130};
    tui_rgb_t accent = th ? th->gradient[4] : (tui_rgb_t){ 74, 222, 128};
    tui_rgb_t line   = th ? tui_color_to_rgb_xterm(th->palette.border) : (tui_rgb_t){ 40,  44,  60};

    int row = area->row;
    int col = area->col;
    int w   = area->cols;

    /* 1. 顶部 1px 极细分隔线 - 极轻的 dim 灰，无 accent 嵌入块 */
    if (area->rows >= 1) {
        tui_cursor_goto(fd, row, col);
        tui_set_bg_rgb(fd, bg);
        tui_set_fg_rgb(fd, line);
        tui_set_attr(fd, TUI_ATTR_DIM);
        for (int i = 0; i < w; i++) tui_write(fd, "─");
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, bg);
    }
    if (area->rows < 2) return TUI_OK;

    /* 内容行 */
    int cy = row + 1;

    /* 背景填充 */
    tui_cursor_goto(fd, cy, col);
    tui_set_bg_rgb(fd, bg);
    tui_spaces(fd, w);
    tui_reset_style(fd);
    tui_set_bg_rgb(fd, bg);

    int x = col;
    int max_x = col + w - 1;

    /* 2. 左侧 accent 小竖条 + padding */
    tui_cursor_goto(fd, cy, x);
    tui_set_bg_rgb(fd, bg);
    tui_set_fg_rgb(fd, accent);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_write(fd, "▎");
    tui_reset_style(fd);
    tui_set_bg_rgb(fd, bg);
    x += 1;
    x += 2;

    /* subtle chip 背景：淡但可见 */
    tui_rgb_t chip_bg = {
        (uint8_t)(bg.r * 0.80 + accent.r * 0.20),
        (uint8_t)(bg.g * 0.80 + accent.g * 0.20),
        (uint8_t)(bg.b * 0.80 + accent.b * 0.20),
    };

    /* 3. 渲染 hint 群：[key] label，组间用 · 分隔 */
    int rendered = 0;
    for (int i = 0; i < kh->nhints; i++) {
        tui_keyhint_t *h = &kh->hints[i];
        if (!h->key) continue;

        /* 不再用每个 hint 自己的 color——保持统一克制风格 */
        (void)h;
        tui_rgb_t kc = accent;

        int klen  = tui_strwidth(h->key);
        int llen  = h->label ? tui_strwidth(h->label) : 0;
        int chip_w = klen + 2;  /* 左右各 1 空格 padding */
        int need  = chip_w + (llen > 0 ? (1 + llen) : 0) + (rendered > 0 ? 3 : 0);
        if (x + need > max_x) break;

        /* 组间分隔：1 个 dot + 2 空格 */
        if (rendered > 0) {
            tui_cursor_goto(fd, cy, x);
            tui_set_bg_rgb(fd, bg);
            tui_set_fg_rgb(fd, dim);
            tui_write(fd, "·");
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, bg);
            x += 3;
        }

        /* key chip: subtle bg + accent bold key */
        tui_cursor_goto(fd, cy, x);
        tui_set_bg_rgb(fd, chip_bg);
        tui_set_fg_rgb(fd, kc);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, " ");
        tui_write(fd, h->key);
        tui_write(fd, " ");
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, bg);

        x += chip_w;

        /* label（fg 文字） */
        if (h->label && llen > 0) {
            tui_cursor_goto(fd, cy, x);
            tui_set_bg_rgb(fd, bg);
            tui_set_fg_rgb(fd, fg);
            tui_set_attr(fd, TUI_ATTR_RESET);
            tui_write(fd, " ");
            tui_write(fd, h->label);
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, bg);
            x += 1 + llen;
        }

        rendered++;
    }

    /* 4. 右侧 status：• theme · HH:MM */
    if (x + 22 < max_x) {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        char ts[16];
        strftime(ts, sizeof(ts), "%H:%M", &tm);

        const char *tname = th ? th->name : "—";
        int tname_w = (int)strlen(tname);

        /* 右侧布局: ● theme · HH:MM
         * 总宽 = 1(●) + 1(空格) + tname_w + 3( · ) + 5(HH:MM) = 10 + tname_w
         */
        int right_total = 1 + 1 + tname_w + 3 + 5;
        int rx = max_x - right_total + 1;
        if (rx < x + 4) rx = x + 4;

        /* ● 状态点（dim） */
        tui_cursor_goto(fd, cy, rx);
        tui_set_bg_rgb(fd, bg);
        tui_set_fg_rgb(fd, dim);
        tui_write(fd, "•");
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, bg);

        /* theme name (fg) */
        tui_cursor_goto(fd, cy, rx + 2);
        tui_set_bg_rgb(fd, bg);
        tui_set_fg_rgb(fd, fg);
        tui_write(fd, tname);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, bg);

        /* separator: " · " */
        tui_cursor_goto(fd, cy, rx + 2 + tname_w);
        tui_set_bg_rgb(fd, bg);
        tui_set_fg_rgb(fd, dim);
        tui_write(fd, " · ");
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, bg);

        /* time (accent) */
        tui_cursor_goto(fd, cy, rx + 2 + tname_w + 3);
        tui_set_bg_rgb(fd, bg);
        tui_set_fg_rgb(fd, accent);
        tui_write(fd, ts);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, bg);
    }

    return TUI_OK;
}

tui_layout_t *tui_keyhints_new(tui_keyhint_t *hints, int nhints)
{
    tui_keyhints_t *kh = (tui_keyhints_t *)calloc(1, sizeof(tui_keyhints_t));
    if (!kh) return NULL;
    kh->hints = hints;
    kh->nhints = nhints;
    return tui_layout_leaf(tui_keyhints_render, kh);
}

/* ══════════════════════════════════════════════════════
 *  Stat 大数字卡
 * ══════════════════════════════════════════════════════ */

int tui_stat_render(int fd, const tui_rect_t *area, void *userdata)
{
    tui_stat_t *s = (tui_stat_t *)userdata;
    if (!s || !area) return TUI_ERR_PARAM;
    if (!tui_rect_valid(area)) return TUI_OK;

    const tui_theme_t *th = tui_theme_current();
    tui_rgb_t surf_bg = th ? th->surface_bg : (tui_rgb_t){13,13,23};
    tui_rgb_t surf_fg = th ? th->surface_fg : (tui_rgb_t){229,229,229};
    tui_rgb_t dim     = (tui_rgb_t){110, 115, 130};

    int y = area->row;
    int x = area->col;

    /* 背景填充 */
    tui_cursor_goto(fd, y, x);
    tui_set_bg_rgb(fd, surf_bg);
    tui_spaces(fd, area->cols);
    tui_reset_style(fd);

    /* 标签（小、dim） */
    tui_cursor_goto(fd, y, x);
    tui_set_bg_rgb(fd, surf_bg);
    tui_set_fg_rgb(fd, dim);
    tui_set_attr(fd, TUI_ATTR_DIM);
    tui_write(fd, s->label);
    tui_reset_style(fd);

    /* 数值（大、加粗） */
    if (area->rows >= 2) {
        tui_cursor_goto(fd, y + 1, x);
        tui_set_bg_rgb(fd, surf_bg);
        tui_rgb_t vc = color_or_theme(s->fg, surf_fg);
        tui_set_fg_rgb(fd, vc);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, s->value);
        tui_reset_style(fd);
    }

    /* 趋势 (3rd line) */
    if (area->rows >= 3 && (s->trend != TUI_TREND_FLAT || s->delta[0])) {
        tui_cursor_goto(fd, y + 2, x);
        tui_set_bg_rgb(fd, surf_bg);
        tui_rgb_t tc = color_or_theme(s->trend_color, dim);

        const char *arrow = "─";
        tui_rgb_t ac = dim;
        if (s->trend > 0)      { arrow = "▲"; ac = color_or_theme(TUI_COLOR_GREEN, tc); }
        else if (s->trend < 0) { arrow = "▼"; ac = color_or_theme(TUI_COLOR_RED,   tc); }

        tui_set_bg_rgb(fd, surf_bg);
        tui_set_fg_rgb(fd, ac);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, arrow);
        if (s->delta[0]) {
            tui_set_attr(fd, TUI_ATTR_RESET);
            tui_set_bg_rgb(fd, surf_bg);
            tui_set_fg_rgb(fd, ac);
            tui_set_attr(fd, TUI_ATTR_DIM);
            tui_write(fd, " ");
            tui_write(fd, s->delta);
        }
        tui_reset_style(fd);
    }

    return TUI_OK;
}

tui_layout_t *tui_stat_new(const char *label, const char *value,
                           tui_color_t fg, int trend, const char *delta)
{
    tui_stat_t *s = (tui_stat_t *)calloc(1, sizeof(tui_stat_t));
    if (!s) return NULL;
    if (label) strncpy(s->label, label, sizeof(s->label) - 1);
    if (value) strncpy(s->value, value, sizeof(s->value) - 1);
    s->fg = fg;
    s->trend = trend;
    if (delta) strncpy(s->delta, delta, sizeof(s->delta) - 1);
    s->trend_color = TUI_COLOR_DEFAULT;
    return tui_layout_leaf(tui_stat_render, s);
}

/* ══════════════════════════════════════════════════════
 *  Sparkline 迷你折线
 * ══════════════════════════════════════════════════════ */

/* 8 段高度字符（U+2581..U+2588） */
static const char *spark_chars[9] = {
    " ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"
};

int tui_sparkline_render(int fd, const tui_rect_t *area, void *userdata)
{
    tui_sparkline_t *sp = (tui_sparkline_t *)userdata;
    if (!sp || !area) return TUI_ERR_PARAM;
    if (!tui_rect_valid(area)) return TUI_OK;
    if (!sp->data || sp->npoints <= 0) return TUI_OK;

    const tui_theme_t *th = tui_theme_current();
    tui_rgb_t surf_bg = th ? th->surface_bg : (tui_rgb_t){13,13,23};
    tui_rgb_t fg = color_or_theme(sp->fg, th ? th->gradient[3] : (tui_rgb_t){74,222,128});
    tui_rgb_t fill = color_or_theme(sp->fill_color, fg);

    int maxv = sp->max_val > 0 ? sp->max_val : 100;
    int n = sp->npoints;
    if (n > area->cols) n = area->cols;

    /* 背景行 */
    tui_cursor_goto(fd, area->row, area->col);
    tui_set_bg_rgb(fd, surf_bg);
    tui_spaces(fd, area->cols);
    tui_reset_style(fd);

    tui_cursor_goto(fd, area->row, area->col);
    tui_set_bg_rgb(fd, surf_bg);
    tui_set_fg_rgb(fd, fg);

    /* 计算起点：让数据居中 */
    int start = (area->cols - n) / 2;
    if (start < 0) start = 0;
    tui_spaces(fd, start);

    /* 渲染：单行 → 高度字符 1..8（每点一个 3-byte UTF-8） */
    for (int i = 0; i < n; i++) {
        int v = sp->data[i];
        if (v < 0) v = 0;
        if (v > maxv) v = maxv;
        int idx = (v * 8 + maxv - 1) / maxv;
        if (idx > 8) idx = 8;
        if (idx < 1) idx = 1;

        tui_set_fg_rgb(fd, fg);
        write(fd, spark_chars[idx], 3);
    }

    tui_reset_style(fd);
    return TUI_OK;
}

tui_layout_t *tui_sparkline_new(const int *data, int npoints, tui_color_t fg)
{
    tui_sparkline_t *sp = (tui_sparkline_t *)calloc(1, sizeof(tui_sparkline_t));
    if (!sp) return NULL;
    sp->data = data;
    sp->npoints = npoints;
    sp->max_val = 100;
    sp->fg = fg;
    sp->filled = 0;
    return tui_layout_leaf(tui_sparkline_render, sp);
}

/* ══════════════════════════════════════════════════════
 *  Card 卡片
 * ══════════════════════════════════════════════════════ */

int tui_card_render(int fd, const tui_rect_t *area, void *userdata)
{
    tui_card_t *c = (tui_card_t *)userdata;
    if (!c || !area) return TUI_ERR_PARAM;
    if (!tui_rect_valid(area)) return TUI_OK;
    if (area->rows < 3 || area->cols < 6) return TUI_OK;

    const tui_theme_t *th = tui_theme_current();
    tui_rgb_t title_fg = color_or_theme(c->title_fg, th ? th->gradient[4] : (tui_rgb_t){74,222,128});
    tui_rgb_t surf_bg  = th ? th->surface_bg : (tui_rgb_t){13,13,23};
    tui_rgb_t surf_fg  = th ? th->surface_fg : (tui_rgb_t){229,229,229};
    tui_rgb_t dim      = (tui_rgb_t){100, 100, 120};

    /* 整个卡片背景：与 surface 几乎相同，仅极轻微提亮（< 3 levels）
     * 避免"外层 surface"和"内层 card"出现明显边界，破坏整体一致性。 */
    tui_rgb_t card_bg = c->bg ? tui_color_to_rgb_xterm(c->bg)
                              : (tui_rgb_t){
                                    (uint8_t)(surf_bg.r + 2),
                                    (uint8_t)(surf_bg.g + 2),
                                    (uint8_t)(surf_bg.b + 2) };
    for (int r = 0; r < area->rows; r++) {
        tui_cursor_goto(fd, area->row + r, area->col);
        tui_set_bg_rgb(fd, card_bg);
        tui_spaces(fd, area->cols);
    }
    tui_reset_style(fd);

    /* 顶部 1 行：标题 + 副标题 */
    int y = area->row;
    int x = area->col;

    tui_cursor_goto(fd, y, x);
    tui_set_bg_rgb(fd, card_bg);
    tui_set_fg_rgb(fd, title_fg);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_write(fd, " ▸ ");
    tui_write(fd, c->title);
    tui_reset_style(fd);

    if (c->subtitle[0]) {
        int sub_w = tui_strwidth(c->subtitle) + 2;
        int right_x = area->col + area->cols - sub_w;
        if (right_x > x + tui_strwidth(c->title) + 6) {
            tui_cursor_goto(fd, y, right_x);
            tui_set_bg_rgb(fd, card_bg);
            tui_set_fg_rgb(fd, dim);
            tui_set_attr(fd, TUI_ATTR_ITALIC);
            tui_write(fd, c->subtitle);
            tui_reset_style(fd);
        }
    }

    /* 标题行下方分隔线 */
    tui_cursor_goto(fd, y + 1, x);
    tui_set_bg_rgb(fd, card_bg);
    tui_set_fg_rgb(fd, dim);
    tui_set_attr(fd, TUI_ATTR_DIM);
    for (int i = 0; i < area->cols; i++) write(fd, "─", 3);
    tui_reset_style(fd);

    /* 渲染内容回调（区域：title 行 + 分隔线之下） */
    if (c->content_fn && area->rows >= 3) {
        tui_rect_t inner = {
            area->row + 2,
            area->col,
            area->rows - 2,
            area->cols
        };
        c->content_fn(fd, &inner, c->content_data);
    }

    return TUI_OK;
}

tui_layout_t *tui_card_new(const char *title, tui_render_fn content_fn, void *data)
{
    tui_card_t *c = (tui_card_t *)calloc(1, sizeof(tui_card_t));
    if (!c) return NULL;
    if (title) strncpy(c->title, title, sizeof(c->title) - 1);
    c->content_fn = content_fn;
    c->content_data = data;
    c->title_fg = TUI_COLOR_DEFAULT;
    c->bg = 0;
    return tui_layout_leaf(tui_card_render, c);
}

/* ══════════════════════════════════════════════════════
 *  Banner v2 — 多种风格 + 渐变标题
 * ══════════════════════════════════════════════════════ */

int tui_banner_render_v2(int fd, const tui_rect_t *area, void *userdata)
{
    tui_banner_v2_t *b = (tui_banner_v2_t *)userdata;
    if (!b || !area) return TUI_ERR_PARAM;
    if (!tui_rect_valid(area)) return TUI_OK;

    const tui_theme_t *th = tui_theme_current();
    tui_rgb_t surf_bg = th ? th->surface_bg : (tui_rgb_t){13,13,23};
    tui_rgb_t surf_fg = th ? th->surface_fg : (tui_rgb_t){229,229,229};
    tui_rgb_t dim     = (tui_rgb_t){100, 100, 120};

    int w = area->cols;
    if (w < 8) return TUI_OK;

    tui_rgb_t accent = color_or_theme(b->color, th ? th->gradient[3] : (tui_rgb_t){74,222,128});

    /* 卡片背景 = surface + 6（与 stat 卡 / service 卡片一致，
     * 否则 banner 区域会比 stat 卡暗一档形成"暗色带"） */
    tui_rgb_t card_bg = {
        (uint8_t)(surf_bg.r + 6),
        (uint8_t)(surf_bg.g + 6),
        (uint8_t)(surf_bg.b + 6)
    };

    /* 风格选择 */
    const char *tl, *tr, *bl, *br, *hz, *vt;
    switch (b->style) {
    case TUI_BANNER_DOUBLE: tl="╔"; tr="╗"; bl="╚"; br="╝"; hz="═"; vt="║"; break;
    case TUI_BANNER_HEAVY:  tl="┏"; tr="┓"; bl="┗"; br="┛"; hz="━"; vt="┃"; break;
    case TUI_BANNER_ANGLED: tl="┌"; tr="┐"; bl="└"; br="┘"; hz="─"; vt="│"; break;
    case TUI_BANNER_SIMPLE:
    default:                tl="┌"; tr="┐"; bl="└"; br="┘"; hz="─"; vt="│"; break;
    }

    int y = area->row;
    int x = area->col;
    int h = area->rows;

    /* 背景：填充整片区域（不只顶行），避免出现暗色"背景缺失"带 */
    for (int r = 0; r < h; r++) {
        tui_cursor_goto(fd, y + r, x);
        tui_set_bg_rgb(fd, card_bg);
        tui_spaces(fd, w);
    }
    tui_reset_style(fd);

    /* 顶行：tag + 装饰线 + 标题 + 装饰线 + corner */
    tui_cursor_goto(fd, y, x);
    tui_set_fg_rgb(fd, accent);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_set_bg_rgb(fd, card_bg);
    write(fd, tl, 3);

    int x_cur = x + 1;
    int x_end = x + w - 1;

    /* 装饰左线（2 段） */
    for (int i = 0; i < 2 && x_cur + 1 <= x_end; i++) {
        write(fd, hz, 3);
        x_cur += 1;
    }

    /* tag (如 "v1.0") */
    if (b->tag && b->tag[0]) {
        tui_set_fg_rgb(fd, dim);
        tui_set_attr(fd, TUI_ATTR_DIM);
        tui_write(fd, " ");
        tui_write(fd, b->tag);
        tui_write(fd, " ");
        int tw = tui_strwidth(b->tag) + 2;
        x_cur += tw;
    }

    /* 标题 */
    if (b->text[0] && x_cur + 1 <= x_end) {
        tui_set_fg_rgb(fd, accent);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, " ");
        x_cur++;

        /* 渐变：每个字符从 dim 渐变到 accent */
        if (b->gradient && th && th->use_24bit && th->gradient[0].r != th->gradient[3].r) {
            int tlen = tui_strwidth(b->text);
            int idx = 0;
            for (const char *p = b->text; *p && x_cur < x_end; ) {
                unsigned char c = (unsigned char)*p;
                int bytes = 1;
                if (c >= 0x80) {
                    if      ((c & 0xE0) == 0xC0) bytes = 2;
                    else if ((c & 0xF0) == 0xE0) bytes = 3;
                    else if ((c & 0xF8) == 0xF0) bytes = 4;
                }
                /* 6 段渐变循环 */
                int g = (idx * 5) / (tlen > 0 ? tlen : 1);
                if (g < 0) g = 0; if (g > 5) g = 5;
                tui_rgb_t gc = th->gradient[g];
                tui_set_fg_rgb(fd, gc);
                tui_set_attr(fd, TUI_ATTR_BOLD);
                tui_set_bg_rgb(fd, card_bg);
                write(fd, p, (size_t)bytes);
                p += bytes;
                x_cur += (c >= 0x80) ? 2 : 1;
                idx++;
            }
        } else {
            int bytes = tui_truncate(b->text, x_end - x_cur);
            write(fd, b->text, (size_t)bytes);
            x_cur += tui_strwidth(b->text);
        }
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, card_bg);
        tui_write(fd, " ");
        x_cur++;
    }

    /* 装饰右线 - 填充到右角前一列 */
    tui_set_fg_rgb(fd, accent);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_set_bg_rgb(fd, card_bg);
    while (x_cur < x_end) {
        write(fd, hz, 3);
        x_cur += 1;
    }

    /* 右上角 (在最后一列) */
    tui_cursor_goto(fd, y, x_end);
    write(fd, tr, 3);
    tui_reset_style(fd);

    /* 副标题行 */
    if (b->sub[0] && area->rows >= 2) {
        y++;
        tui_cursor_goto(fd, y, x);
        tui_set_bg_rgb(fd, card_bg);
        tui_set_fg_rgb(fd, accent);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        write(fd, vt, 3);
        tui_set_attr(fd, TUI_ATTR_RESET);

        int sub_w = tui_strwidth(b->sub);
        int inner_w = w - 2;  /* 减左右边框 */
        int sub_pad = (inner_w - sub_w) / 2;
        if (sub_pad < 0) sub_pad = 0;

        tui_spaces(fd, sub_pad);
        tui_set_fg_rgb(fd, dim);
        tui_set_attr(fd, TUI_ATTR_ITALIC);
        tui_set_bg_rgb(fd, card_bg);
        int sbytes = tui_truncate(b->sub, inner_w - sub_pad);
        write(fd, b->sub, (size_t)sbytes);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, card_bg);

        int rem = inner_w - sub_pad - sub_w;
        if (rem < 0) rem = 0;
        tui_spaces(fd, rem);

        tui_set_fg_rgb(fd, accent);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_set_bg_rgb(fd, card_bg);
        tui_cursor_goto(fd, y, x + w - 1);
        write(fd, vt, 3);
        tui_reset_style(fd);
    }

    /* 底行 */
    if (area->rows >= 2 + (b->sub[0] ? 1 : 0)) {
        y++;
        tui_cursor_goto(fd, y, x);
        tui_set_fg_rgb(fd, accent);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_set_bg_rgb(fd, card_bg);
        write(fd, bl, 3);
        /* hz 每个占 1 列 (3 字节 UTF-8) */
        for (int i = 0; i < w - 2; i++)
            write(fd, hz, 3);
        tui_cursor_goto(fd, y, x + w - 1);
        write(fd, br, 3);
        tui_reset_style(fd);
    }

    return TUI_OK;
}

tui_layout_t *tui_banner_v2(const tui_banner_v2_t *cfg)
{
    if (!cfg) return NULL;
    tui_banner_v2_t *heap = (tui_banner_v2_t *)calloc(1, sizeof(tui_banner_v2_t));
    if (!heap) return NULL;
    *heap = *cfg;
    return tui_layout_leaf_with_free(tui_banner_render, heap,
                                     (void (*)(void *))free);
}

/* ══════════════════════════════════════════════════════
 *  Progress v2 — 多种风格
 * ══════════════════════════════════════════════════════ */

tui_layout_t *tui_progress_v2(const tui_progress_v2_t *cfg)
{
    if (!cfg) return NULL;
    tui_progress_v2_t *heap = (tui_progress_v2_t *)calloc(1, sizeof(tui_progress_v2_t));
    if (!heap) return NULL;
    *heap = *cfg;
    return tui_layout_leaf_with_free(tui_progress_render_v2, heap,
                                     (void (*)(void *))free);
}

int tui_progress_render_v2(int fd, const tui_rect_t *area, void *userdata)
{
    tui_progress_v2_t *p = (tui_progress_v2_t *)userdata;
    if (!p || !area) return TUI_ERR_PARAM;

    const tui_theme_t *th = tui_theme_current();
    tui_rgb_t surf_bg = th ? th->surface_bg : (tui_rgb_t){13,13,23};
    tui_rgb_t dim = (tui_rgb_t){90, 90, 100};
    tui_rgb_t fc = color_or_theme(p->fill_color, th ? th->gradient[3] : (tui_rgb_t){74,222,128});
    tui_rgb_t ec = color_or_theme(p->empty_color, dim);
    tui_rgb_t lc = color_or_theme(p->label_color, dim);

    int label_w = p->label[0] ? tui_strwidth(p->label) + 1 : 0;
    int pct_w = p->show_percent ? 6 : 0;
    int avail = area->cols - label_w - pct_w;
    if (avail < 4) avail = 4;

    int bar_w = p->bar_width > 0
        ? (p->bar_width < avail ? p->bar_width : avail)
        : avail;
    if (bar_w < 4) bar_w = 4;

    int filled = (int)(p->value * bar_w);
    if (filled < 0)   filled = 0;
    if (filled > bar_w) filled = bar_w;

    tui_cursor_goto(fd, area->row, area->col);

    if (p->label[0]) {
        tui_set_bg_rgb(fd, surf_bg);
        tui_set_fg_rgb(fd, lc);
        tui_set_attr(fd, TUI_ATTR_DIM);
        tui_write(fd, p->label);
        tui_write(fd, " ");
        tui_reset_style(fd);
    }

    int i;
    const char *ch_full, *ch_empty;
    switch (p->style) {
    case TUI_PROGRESS_SEGMENT: ch_full = "▰"; ch_empty = "▱"; break;
    case TUI_PROGRESS_DOT:     ch_full = "●"; ch_empty = "○"; break;
    case TUI_PROGRESS_PIPE:    ch_full = "━"; ch_empty = "─"; break;
    case TUI_PROGRESS_SHADE:   ch_full = "█"; ch_empty = "░"; break;
    case TUI_PROGRESS_BLOCK:
    default:                   ch_full = "█"; ch_empty = "░"; break;
    }

    tui_set_bg_rgb(fd, surf_bg);

    for (i = 0; i < filled; i++) {
        if (p->style == TUI_PROGRESS_SHADE) {
            /* 渐变：0..25% 浅 25..50% 中 50..100% 满 */
            double ratio = (double)i / (double)(bar_w > 0 ? bar_w : 1);
            tui_rgb_t c;
            if      (ratio < 0.33) c = th ? th->gradient[2] : (tui_rgb_t){100,200,150};
            else if (ratio < 0.66) c = th ? th->gradient[3] : (tui_rgb_t){ 74,222,128};
            else                   c = th ? th->gradient[4] : (tui_rgb_t){ 46,160, 67};
            tui_set_fg_rgb(fd, c);
        } else {
            tui_set_fg_rgb(fd, fc);
        }
        tui_set_attr(fd, TUI_ATTR_BOLD);
        write(fd, ch_full, 3);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, surf_bg);
    }

    /* 边缘字符（高亮头） */
    if (filled > 0 && filled < bar_w) {
        tui_set_fg_rgb(fd, th ? th->gradient[5] : (tui_rgb_t){22,101,52});
        tui_set_attr(fd, TUI_ATTR_BOLD);
        write(fd, "▏", 3);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, surf_bg);
    }

    tui_set_fg_rgb(fd, ec);
    tui_set_attr(fd, TUI_ATTR_DIM);
    for (i = filled + (filled > 0 ? 1 : 0); i < bar_w; i++)
        write(fd, ch_empty, 3);
    tui_reset_style(fd);
    tui_set_bg_rgb(fd, surf_bg);

    if (p->show_percent) {
        int pct = (int)(p->value * 100.0);
        if (pct > 100) pct = 100;
        tui_set_fg_rgb(fd, dim);
        tui_set_attr(fd, TUI_ATTR_DIM);
        tui_printf(fd, " %3d%%", pct);
        tui_reset_style(fd);
    }

    return TUI_OK;
}

/* ══════════════════════════════════════════════════════
 *  Spinner v2 — 多种风格
 * ══════════════════════════════════════════════════════ */

tui_layout_t *tui_spinner_v2(const tui_spinner_v2_t *cfg)
{
    if (!cfg) return NULL;
    tui_spinner_v2_t *heap = (tui_spinner_v2_t *)calloc(1, sizeof(tui_spinner_v2_t));
    if (!heap) return NULL;
    *heap = *cfg;
    if (!heap->frames) heap->frames = TUI_SPINNER_DOTS;
    return tui_layout_leaf_with_free(tui_spinner_render_v2, heap,
                                     (void (*)(void *))free);
}

int tui_spinner_render_v2(int fd, const tui_rect_t *area, void *userdata)
{
    tui_spinner_v2_t *s = (tui_spinner_v2_t *)userdata;
    if (!s || !area) return TUI_ERR_PARAM;

    const tui_theme_t *th = tui_theme_current();
    tui_rgb_t surf_bg = th ? th->surface_bg : (tui_rgb_t){13,13,23};

    const char *frames = s->frames ? s->frames : TUI_SPINNER_DOTS;
    int nframes = (int)strlen(frames) / 3;
    if (nframes == 0) nframes = 1;
    int idx = (s->frame % nframes) * 3;

    tui_cursor_goto(fd, area->row, area->col);
    tui_set_bg_rgb(fd, surf_bg);
    tui_set_fg_rgb(fd, color_or_theme(s->color, th ? th->gradient[4] : (tui_rgb_t){74,222,128}));
    tui_set_attr(fd, TUI_ATTR_BOLD);
    write(fd, frames + idx, 3);
    tui_reset_style(fd);

    if (s->label[0]) {
        tui_set_bg_rgb(fd, surf_bg);
        tui_write(fd, " ");
        tui_set_fg_rgb(fd, th ? th->surface_fg : (tui_rgb_t){229,229,229});
        tui_set_attr(fd, TUI_ATTR_DIM);
        tui_write(fd, s->label);
        tui_reset_style(fd);
    }

    return TUI_OK;
}

/* ══════════════════════════════════════════════════════
 *  Badge v2 — 多种风格
 * ══════════════════════════════════════════════════════ */

tui_layout_t *tui_badge_v2(const tui_badge_v2_t *cfg)
{
    if (!cfg) return NULL;
    tui_badge_v2_t *heap = (tui_badge_v2_t *)calloc(1, sizeof(tui_badge_v2_t));
    if (!heap) return NULL;
    *heap = *cfg;
    return tui_layout_leaf_with_free(tui_badge_render_v2, heap,
                                     (void (*)(void *))free);
}

int tui_badge_render_v2(int fd, const tui_rect_t *area, void *userdata)
{
    tui_badge_v2_t *b = (tui_badge_v2_t *)userdata;
    if (!b || !area) return TUI_ERR_PARAM;
    if (!tui_rect_valid(area)) return TUI_OK;

    const tui_theme_t *th = tui_theme_current();
    tui_rgb_t surf_bg = th ? th->surface_bg : (tui_rgb_t){13,13,23};

    int len = tui_strwidth(b->text);
    if (len > area->cols) len = area->cols;

    tui_rgb_t bg = color_or_theme(b->bg, th ? th->gradient[3] : (tui_rgb_t){74,222,128});
    tui_rgb_t fg = color_or_theme(b->fg, (tui_rgb_t){255,255,255});

    tui_cursor_goto(fd, area->row, area->col);

    switch (b->style) {
    case TUI_BADGE_PILL: {
        tui_set_bg_rgb(fd, surf_bg);
        tui_set_fg_rgb(fd, bg);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, "●  ");
        tui_set_bg_rgb(fd, surf_bg);
        tui_set_fg_rgb(fd, th ? th->surface_fg : (tui_rgb_t){229,229,229});
        tui_set_attr(fd, TUI_ATTR_RESET);
        int bytes = tui_truncate(b->text, area->cols - 3);
        write(fd, b->text, (size_t)bytes);
        tui_reset_style(fd);
        break;
    }
    case TUI_BADGE_OUTLN: {
        tui_set_bg_rgb(fd, surf_bg);
        tui_set_fg_rgb(fd, bg);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, "[ ");
        tui_set_bg_rgb(fd, surf_bg);
        tui_set_fg_rgb(fd, th ? th->surface_fg : (tui_rgb_t){229,229,229});
        tui_set_attr(fd, TUI_ATTR_RESET);
        int bytes = tui_truncate(b->text, area->cols - 4);
        write(fd, b->text, (size_t)bytes);
        tui_set_bg_rgb(fd, surf_bg);
        tui_set_fg_rgb(fd, bg);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, " ]");
        tui_reset_style(fd);
        break;
    }
    case TUI_BADGE_DOT: {
        tui_set_bg_rgb(fd, surf_bg);
        tui_set_fg_rgb(fd, bg);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, "● ");
        tui_set_bg_rgb(fd, surf_bg);
        tui_set_fg_rgb(fd, th ? th->surface_fg : (tui_rgb_t){229,229,229});
        tui_set_attr(fd, TUI_ATTR_RESET);
        int bytes = tui_truncate(b->text, area->cols - 2);
        write(fd, b->text, (size_t)bytes);
        tui_reset_style(fd);
        break;
    }
    case TUI_BADGE_TAG: {
        tui_set_bg_rgb(fd, surf_bg);
        tui_set_fg_rgb(fd, th ? th->gradient[2] : (tui_rgb_t){100,200,150});
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, "#");
        tui_set_bg_rgb(fd, surf_bg);
        tui_set_fg_rgb(fd, th ? th->surface_fg : (tui_rgb_t){229,229,229});
        tui_set_attr(fd, TUI_ATTR_RESET);
        int bytes = tui_truncate(b->text, area->cols - 1);
        write(fd, b->text, (size_t)bytes);
        tui_reset_style(fd);
        break;
    }
    case TUI_BADGE_FILL:
    default: {
        /* 原 Badge 行为 */
        tui_set_bg_rgb(fd, bg);
        tui_set_fg_rgb(fd, fg);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_spaces(fd, 1);
        int bytes = tui_truncate(b->text, area->cols - 2);
        write(fd, b->text, (size_t)bytes);
        tui_spaces(fd, 1);
        tui_reset_style(fd);
        break;
    }
    }

    tui_reset_style(fd);
    return TUI_OK;
}

/* ══════════════════════════════════════════════════════
 *  Table v2 — 斑马纹 + 圆角
 *
 *  通过扩展 tui_table_t 字段实现
 * ══════════════════════════════════════════════════════ */

tui_table_t *tui_table_new_v2(tui_column_t *cols, int ncols,
                              tui_table_cell_fn cell_fn, int nrows,
                              const tui_table_style_t *style)
{
    (void)style;  /* 暂未使用；后续扩展 */
    tui_table_t *t = (tui_table_t *)calloc(1, sizeof(tui_table_t));
    if (!t) return NULL;
    for (int i = 0; i < ncols && i < TUI_TABLE_MAX_COLS; i++) t->columns[i] = cols[i];
    t->ncols = ncols;
    t->nrows = nrows;
    t->cell_fn = cell_fn;
    t->selected = -1;
    t->header_bg = TUI_COLOR_DEFAULT;
    t->select_bg = TUI_COLOR_DEFAULT;
    return t;
}

/* ══════════════════════════════════════════════════════
 *  tui_layout_leaf_with_free
 *  让 layout 自动释放用户数据（用于 *_v2 工厂）
 * ══════════════════════════════════════════════════════ */
