/* widgets_viz.c — meuos-libtui 可视化组件
 *
 * 高级可视化组件：
 *   - Gauge         仪表盘（半圆 / 圆弧 / 线性）
 *   - Heatmap       热力图（git-style 贡献墙）
 *   - BarChart      条形图（横向 / 纵向）
 *   - LineChart     多线对比图
 *   - Marquee       跑马灯 / 滚动字幕
 *   - TreeNode      文件树
 *   - Ring          环形进度
 *   - ActivityLog   时间戳日志流
 *
 * 所有组件基于 tui_theme_current() 读取颜色，支持 24-bit 真彩色。
 */

#define _XOPEN_SOURCE 700

#include "meuos/libtui.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <math.h>

/* ══════════════════════════════════════════════════════
 *  内部辅助
 * ══════════════════════════════════════════════════════ */

static tui_rgb_t v3_surf_bg(void)
{
    const tui_theme_t *th = tui_theme_current();
    return th ? th->surface_bg : (tui_rgb_t){13, 13, 23};
}

static tui_rgb_t v3_surf_fg(void)
{
    const tui_theme_t *th = tui_theme_current();
    return th ? th->surface_fg : (tui_rgb_t){229, 229, 229};
}

static tui_rgb_t v3_dim(void)
{
    return (tui_rgb_t){100, 100, 120};
}

static tui_rgb_t v3_grad(int idx)
{
    const tui_theme_t *th = tui_theme_current();
    if (!th) return (tui_rgb_t){200, 200, 200};
    int n = (int)(sizeof(th->gradient) / sizeof(th->gradient[0]));
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    return th->gradient[idx];
}

static tui_rgb_t v3_mix(tui_rgb_t a, tui_rgb_t b, double t)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return (tui_rgb_t){
        (uint8_t)(a.r * (1 - t) + b.r * t),
        (uint8_t)(a.g * (1 - t) + b.g * t),
        (uint8_t)(a.b * (1 - t) + b.b * t),
    };
}

/* ══════════════════════════════════════════════════════
 *  Gauge 仪表盘
 *
 *  4 种风格：TUI_GAUGE_LINEAR / RING / BAR / BULLET
 *  (enums 在 libtui.h 中定义)
 *  (struct tui_gauge_t 在 libtui.h 中定义)
 * ══════════════════════════════════════════════════════ */

int tui_gauge_render(int fd, const tui_rect_t *area, void *userdata)
{
    tui_gauge_t *g = (tui_gauge_t *)userdata;
    if (!g || !area) return TUI_ERR_PARAM;
    if (!tui_rect_valid(area)) return TUI_OK;

    tui_rgb_t bg     = (g->bg.r == 0 && g->bg.g == 0 && g->bg.b == 0) ? v3_surf_bg() : g->bg;
    tui_rgb_t fg     = g->color;
    tui_rgb_t dim    = v3_dim();

    int y = area->row;
    int x = area->col;
    int w = area->cols;
    int h = area->rows;

    /* 背景填充 */
    for (int r = 0; r < h; r++) {
        tui_cursor_goto(fd, y + r, x);
        tui_set_bg_rgb(fd, bg);
        tui_spaces(fd, w);
    }
    tui_reset_style(fd);

    if (h < 1 || w < 4) return TUI_OK;

    /* 标签 */
    if (g->label[0] && h >= 1) {
        tui_cursor_goto(fd, y, x);
        tui_set_bg_rgb(fd, bg);
        tui_set_fg_rgb(fd, dim);
        tui_set_attr(fd, TUI_ATTR_DIM);
        tui_write(fd, g->label);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, bg);
    }

    /* 数值 */
    if (g->show_value && h >= 1) {
        char vs[16];
        snprintf(vs, sizeof(vs), "%d%%", (int)(g->value * 100));
        int vw = tui_strwidth(vs);
        tui_cursor_goto(fd, y, x + w - vw - 1);
        tui_set_bg_rgb(fd, bg);
        tui_set_fg_rgb(fd, fg);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, vs);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, bg);
    }

    if (h < 2) return TUI_OK;

    int bar_y = y + 1;
    int bar_h = h - 1;
    int bar_w = w;

    if (g->style == TUI_GAUGE_LINEAR) {
        /* 线性进度 + 刻度：先画完整轨道，再覆盖填充段 */
        int cy = bar_y + bar_h / 2;
        if (cy >= area->row + h) cy = area->row + h - 1;

        int inner_w = bar_w - 2;
        int fill_w = (int)(g->value * inner_w);
        if (fill_w < 0) fill_w = 0;
        if (fill_w > inner_w) fill_w = inner_w;

        /* 底色行（带左右括号） */
        tui_cursor_goto(fd, cy, x);
        tui_set_bg_rgb(fd, bg);
        tui_set_fg_rgb(fd, dim);
        tui_set_attr(fd, TUI_ATTR_DIM);
        tui_write(fd, "▕");
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, bg);

        /* 完整轨道（暗色） */
        for (int i = 0; i < inner_w; i++) {
            tui_cursor_goto(fd, cy, x + 1 + i);
            tui_set_bg_rgb(fd, bg);
            tui_set_fg_rgb(fd, dim);
            tui_set_attr(fd, TUI_ATTR_DIM);
            write(fd, "─", 3);
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, bg);
        }

        /* 填充段（亮色渐变） */
        for (int i = 0; i < fill_w; i++) {
            double ratio = (double)i / (double)(inner_w > 0 ? inner_w : 1);
            tui_rgb_t c = v3_mix(dim, fg, ratio);
            tui_cursor_goto(fd, cy, x + 1 + i);
            tui_set_bg_rgb(fd, bg);
            tui_set_fg_rgb(fd, c);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            write(fd, "━", 3);
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, bg);
        }

        /* 头部指示器（填充末端的亮点） */
        if (fill_w > 0 && fill_w <= inner_w) {
            tui_cursor_goto(fd, cy, x + fill_w);
            tui_set_bg_rgb(fd, bg);
            tui_set_fg_rgb(fd, fg);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            write(fd, "●", 3);
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, bg);
        }

        /* 行尾括号 */
        tui_cursor_goto(fd, cy, x + bar_w - 1);
        tui_set_bg_rgb(fd, bg);
        tui_set_fg_rgb(fd, dim);
        tui_set_attr(fd, TUI_ATTR_DIM);
        tui_write(fd, "▏");
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, bg);

    } else if (g->style == TUI_GAUGE_BAR) {
        /* 8 段高度字符（U+2581..U+2588） */
        static const char *bch[9] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
        int levels = 8;
        int filled = (int)(g->value * levels);
        if (filled < 0) filled = 0;
        if (filled > levels) filled = levels;

        int cy = bar_y + bar_h / 2;
        if (cy >= area->row + h) cy = area->row + h - 1;
        if (cy < bar_y) cy = bar_y;
        int n = bar_w;
        if (n > bar_w) n = bar_w;
        int start_x = x;
        for (int i = 0; i < n; i++) {
            /* 每列根据位置决定高度字符 */
            double ratio = (double)i / (double)(n > 0 ? n - 1 : 1);
            /* 渐变效果: 越往右越高（如果 progress 是从左到右）*/
            double cell_v = g->value * (i + 1) / n;
            int level = (int)(cell_v * levels);
            if (level < 0) level = 0;
            if (level > levels) level = levels;
            tui_cursor_goto(fd, cy, start_x + i);
            tui_set_bg_rgb(fd, bg);
            tui_rgb_t c = v3_mix(dim, fg, ratio);
            tui_set_fg_rgb(fd, c);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            write(fd, bch[level], 3);
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, bg);
        }
    } else if (g->style == TUI_GAUGE_BULLET) {
        /* 块状指示器：●○○○ - 紧凑无空隙 */
        int total = bar_w;
        if (total < 1) total = 1;
        if (total > 32) total = 32;
        int filled = (int)(g->value * total);
        if (filled < 0) filled = 0;
        if (filled > total) filled = total;
        int cy = bar_y + bar_h / 2;
        if (cy >= area->row + h) cy = area->row + h - 1;
        for (int i = 0; i < total; i++) {
            tui_cursor_goto(fd, cy, x + i);
            tui_set_bg_rgb(fd, bg);
            tui_set_fg_rgb(fd, i < filled ? fg : dim);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            write(fd, i < filled ? "●" : "○", 3);
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, bg);
        }
    } else if (g->style == TUI_GAUGE_RING) {
        /* 多段弧形：使用 ● 字符不同密度表示 */
        int cy = bar_y + bar_h / 2;
        if (cy >= area->row + h) cy = area->row + h - 1;
        if (cy < bar_y) cy = bar_y;

        /* 中心: 百分比 + 标签 */
        char vs[16];
        snprintf(vs, sizeof(vs), "%d%%", (int)(g->value * 100));
        int vs_w = tui_strwidth(vs);
        int cx = x + (bar_w - vs_w) / 2;

        /* 左半填充 */
        int total = (bar_w - 4) / 2;
        if (total < 1) total = 1;
        int filled = (int)(g->value * total);
        if (filled < 0) filled = 0;
        if (filled > total) filled = total;

        /* 左半圆弧 */
        for (int i = 0; i < total; i++) {
            tui_cursor_goto(fd, cy, x + 1 + i);
            tui_set_bg_rgb(fd, bg);
            tui_set_fg_rgb(fd, i < filled ? fg : dim);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            write(fd, "━", 3);
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, bg);
        }
        /* 中心百分比 */
        tui_cursor_goto(fd, cy, cx);
        tui_set_bg_rgb(fd, bg);
        tui_set_fg_rgb(fd, fg);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, vs);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, bg);
        /* 右半圆弧 */
        int right_x = cx + vs_w + 1;
        for (int i = 0; i < total; i++) {
            if (right_x + i >= x + bar_w) break;
            tui_cursor_goto(fd, cy, right_x + i);
            tui_set_bg_rgb(fd, bg);
            tui_set_fg_rgb(fd, (total - 1 - i) < filled ? fg : dim);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            write(fd, "━", 3);
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, bg);
        }
    }

    return TUI_OK;
}

tui_layout_t *tui_gauge_new(const char *label, double value,
                            tui_rgb_t color, int style)
{
    tui_gauge_t *g = (tui_gauge_t *)calloc(1, sizeof(tui_gauge_t));
    if (!g) return NULL;
    if (label) strncpy(g->label, label, sizeof(g->label) - 1);
    g->value = value;
    g->color = color;
    g->style = style;
    g->show_value = 1;
    g->ticks = 5;
    return tui_layout_leaf_with_free(tui_gauge_render, g, free);
}

/* ══════════════════════════════════════════════════════
 *  BigNum 大数字 (数字时钟风格 · 3×5 网格)
 *
 *  经典数字时钟 / LCD 字体：每个数字 3 列 × 5 行 = 15 个像素位。
 *  使用全块字符 █ (U+2588) 渲染高对比度像素，空像素用空格。
 *
 *  字形 (3×5)：
 *      0         1         2         3         4
 *    ###       .#.       ###       ###       #.#
 *    #.#       ##.       ..#       ..#       #.#
 *    #.#       .#.       ###       ###       ###
 *    #.#       .#.       #..       ..#       ..#
 *    ###       ###       ###       ###       ..#
 *
 *      5         6         7         8         9
 *    ###       ###       ###       ###       ###
 *    #..       #..       ..#       #.#       #.#
 *    ###       ###       ..#       ###       ###
 *    ..#       #.#       ..#       #.#       ..#
 *    ###       ###       ..#       ###       ###
 * ══════════════════════════════════════════════════════ */

/* 3-col × 5-row 数字时钟字形（每行 3 个字符） */
static const char *bignum_patterns[10] = {
    /* 0 */  "███\n█ █\n█ █\n█ █\n███",
    /* 1 */  " █ \n██ \n █ \n █ \n███",
    /* 2 */  "███\n  █\n███\n█  \n███",
    /* 3 */  "███\n  █\n███\n  █\n███",
    /* 4 */  "█ █\n█ █\n███\n  █\n  █",
    /* 5 */  "███\n█  \n███\n  █\n███",
    /* 6 */  "███\n█  \n███\n█ █\n███",
    /* 7 */  "███\n  █\n  █\n  █\n  █",
    /* 8 */  "███\n█ █\n███\n█ █\n███",
    /* 9 */  "███\n█ █\n███\n  █\n███",
};

/* 数字高度（5 个 TUI 行） */
#define BIGNUM_ROWS 5
/* 数字宽度（3 列） */
#define BIGNUM_COLS 3

/* 渲染 5 行大数字到指定位置（每数字 3 列宽，5 行高）
 *
 * 关键：█ 是 3 字节 UTF-8 字符（U+2588 = E2 96 88）。
 *      必须按字节切片并保留多字节边界。 */
static void bignum_draw_digit(int fd, int row, int col, tui_rgb_t fg,
                              tui_rgb_t bg, int digit)
{
    if (digit < 0 || digit > 9) return;

    const char *pat = bignum_patterns[digit];

    /* 把 5 行字形解析为 5 个字符串（每行 3 字符 = 9 字节 + \0） */
    char row_buf[BIGNUM_ROWS][16];
    int ri = 0;
    int bi = 0;
    int char_count = 0;

    for (int i = 0; pat[i] && ri < BIGNUM_ROWS; i++) {
        unsigned char c = (unsigned char)pat[i];
        if (c == '\n') {
            /* 当前行结束，补齐到 BIGNUM_COLS 字符（每字符填 1 字节空格） */
            while (char_count < BIGNUM_COLS) {
                row_buf[ri][bi++] = ' ';
                char_count++;
            }
            row_buf[ri][bi] = 0;
            ri++;
            bi = 0;
            char_count = 0;
        } else if (c < 0x80) {
            /* ASCII：1 字节 */
            if (char_count < BIGNUM_COLS) {
                row_buf[ri][bi++] = c;
                char_count++;
            }
        } else {
            /* 多字节 UTF-8（这里只有 █，3 字节）：整体复制 */
            int bytes = 1;
            if ((c & 0xE0) == 0xC0) bytes = 2;
            else if ((c & 0xF0) == 0xE0) bytes = 3;
            else if ((c & 0xF8) == 0xF0) bytes = 4;
            if (char_count < BIGNUM_COLS) {
                for (int k = 0; k < bytes && pat[i + k]; k++) {
                    row_buf[ri][bi++] = pat[i + k];
                }
                i += bytes - 1;
                char_count++;
            }
        }
    }
    while (ri < BIGNUM_ROWS) {
        while (char_count < BIGNUM_COLS) {
            row_buf[ri][bi++] = ' ';
            char_count++;
        }
        row_buf[ri][bi] = 0;
        ri++;
        bi = 0;
        char_count = 0;
    }

    for (int r = 0; r < BIGNUM_ROWS; r++) {
        tui_cursor_goto(fd, row + r, col);
        tui_set_bg_rgb(fd, bg);
        tui_set_fg_rgb(fd, fg);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, row_buf[r]);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, bg);
    }
}

/* 渲染完整数字字符串（每个数字 3 列宽 × 3 行高，数字之间 1 列间隔）
 *
 * 数字间的 1 列间隔对应亚像素行的"空"段，避免连续数字粘连。
 * 小数点 "." / "," 用半块 ▄ 放在最底亚像素行（row+2 行的下半部分）。
 * 单位/标点字符用 ASCII 小字，1 列，放在底行与数字底部对齐。 */
int tui_bignum_render_at(int fd, int row, int col, tui_rgb_t fg, tui_rgb_t bg,
                         const char *num_str)
{
    if (!num_str) return TUI_ERR_PARAM;
    int x = col;
    int len = (int)strlen(num_str);
    for (int i = 0; i < len; i++) {
        char c = num_str[i];
        if (c >= '0' && c <= '9') {
            bignum_draw_digit(fd, row, x, fg, bg, c - '0');
            x += BIGNUM_COLS;  /* 3 列宽 */
            /* 若下一个字符仍是数字，插入 1 列空格防止连字混淆 */
            if (i + 1 < len && num_str[i + 1] >= '0' && num_str[i + 1] <= '9') {
                for (int r = 0; r < BIGNUM_ROWS; r++) {
                    tui_cursor_goto(fd, row + r, x);
                    tui_set_bg_rgb(fd, bg);
                    tui_write(fd, " ");
                }
                tui_reset_style(fd);
                tui_set_bg_rgb(fd, bg);
                x += 1;
            }
        } else if (c == '.' || c == ',') {
            /* 小数点：1 列，放在最底亚像素行（row+2 行的下半部分 = ▄） */
            tui_cursor_goto(fd, row + BIGNUM_ROWS - 1, x);
            tui_set_bg_rgb(fd, bg);
            tui_set_fg_rgb(fd, fg);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            tui_write(fd, "▄");
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, bg);
            x += 1;
        } else if (c == '%' || c == 'G' || c == 'M' || c == 'B' || c == '/'
                   || c == 's' || c == 'm' || c == 'h' || c == 'd') {
            /* 单位字符：单独占 1 列（小字，仅在底行显示，与数字底部对齐） */
            tui_cursor_goto(fd, row + BIGNUM_ROWS - 1, x);
            tui_set_bg_rgb(fd, bg);
            tui_set_fg_rgb(fd, fg);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            char s[2] = { c, 0 };
            tui_write(fd, s);
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, bg);
            x += 1;
        } else if (c == ' ') {
            x += 1;  /* space 占 1 列 */
        } else {
            /* 其他字符：1 列（仅底行） */
            tui_cursor_goto(fd, row + BIGNUM_ROWS - 1, x);
            tui_set_bg_rgb(fd, bg);
            tui_set_fg_rgb(fd, fg);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            char s[2] = { c, 0 };
            tui_write(fd, s);
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, bg);
            x += 1;
        }
    }
    return TUI_OK;
}

/* 计算大数字宽度（用于居中；数字之间含 1 列间隔） */
int tui_bignum_width(const char *num_str)
{
    if (!num_str) return 0;
    int w = 0;
    for (int i = 0; num_str[i]; i++) {
        char c = num_str[i];
        if (c >= '0' && c <= '9') {
            w += BIGNUM_COLS;  /* 3 列宽 */
            if (num_str[i + 1] >= '0' && num_str[i + 1] <= '9')
                w += 1;  /* 数字间 1 列间隔 */
        } else if (c == '.' || c == ',') w += 1;
        else w += 1;
    }
    return w;
}

/* ══════════════════════════════════════════════════════
 *  Heatmap 热力图
 *
 *  适合显示 Git 贡献墙、监控时序热力、CPU 核心使用率等
 *  (struct tui_heatmap_t 在 libtui.h 中定义)
 * ══════════════════════════════════════════════════════ */

int tui_heatmap_render(int fd, const tui_rect_t *area, void *userdata)
{
    tui_heatmap_t *hm = (tui_heatmap_t *)userdata;
    if (!hm || !area) return TUI_ERR_PARAM;
    if (!tui_rect_valid(area) || !hm->data) return TUI_OK;

    tui_rgb_t bg = (hm->bg.r == 0 && hm->bg.g == 0 && hm->bg.b == 0)
        ? v3_surf_bg() : hm->bg;

    int label_w = hm->show_row_labels && hm->row_labels ? 6 : 0;
    int avail_w = area->cols - label_w;
    int avail_h = area->rows;

    /* 计算每格尺寸 - 自动缩放列数以填满空间 */
    int ch = hm->cell_h > 0 ? hm->cell_h : 1;
    if (ch > 2) ch = 2;  /* 最多 2 行高 */

    int vis_rows = avail_h / ch;
    int vis_cols = avail_w;  /* 每格 1 列 */

    int max_rows = hm->rows < vis_rows ? hm->rows : vis_rows;
    int max_cols = hm->cols < vis_cols ? hm->cols : vis_cols;

    /* 5 级字符 - 全部 3 字节 / 1 col (block elements) */
    static const char *lvl[] = {" ", "░", "▒", "▓", "█"};

    tui_rgb_t base  = v3_grad(0);
    tui_rgb_t dim   = v3_dim();
    tui_rgb_t empty = (tui_rgb_t){30, 30, 40};

    /* 计算每列重复次数 (auto-stretch) */
    int repeat = 1;
    int col_start_offset = 0;
    if (max_cols > 0 && avail_w > max_cols) {
        /* 计算: 让数据填满左侧 (avail_w * 7/10) */
        int target = (avail_w * 7) / 10;
        if (target > max_cols) {
            repeat = target / max_cols;
            if (repeat < 1) repeat = 1;
            col_start_offset = 0;
        }
    }

    for (int r = 0; r < max_rows; r++) {
        for (int rr = 0; rr < ch; rr++) {
            int cy = area->row + r * ch + rr;

            /* 整行先填背景，保证 label 区外、repeat 后、末尾都覆盖到 bg */
            tui_cursor_goto(fd, cy, area->col);
            tui_set_bg_rgb(fd, bg);
            tui_spaces(fd, area->cols);
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, bg);

            /* 行标签 */
            if (label_w > 0 && rr == ch / 2) {
                if (hm->row_labels && hm->row_labels[r]) {
                    tui_cursor_goto(fd, cy, area->col);
                    tui_set_bg_rgb(fd, bg);
                    tui_set_fg_rgb(fd, dim);
                    tui_set_attr(fd, TUI_ATTR_DIM);
                    tui_write(fd, hm->row_labels[r]);
                    /* 补足到 label_w */
                    int lw = tui_strwidth(hm->row_labels[r]);
                    for (int p = 0; p < label_w - lw; p++)
                        write(fd, " ", 1);
                    tui_reset_style(fd);
                    tui_set_bg_rgb(fd, bg);
                }
            }

            /* 数据单元 - 每格 repeat 列宽 */
            for (int c = 0; c < max_cols; c++) {
                double v = hm->data[r * hm->cols + c];
                if (v < 0) v = 0;
                if (v > 1) v = 1;
                int idx = (int)(v * 4.999);
                if (idx < 0) idx = 0;
                if (idx > 4) idx = 4;

                /* 在 (label_w + c*repeat) 位置画 repeat 个 col */
                for (int rep = 0; rep < repeat; rep++) {
                    int cc = area->col + label_w + c * repeat + rep;
                    if (cc > area->col + area->cols) break;
                    tui_cursor_goto(fd, cy, cc);
                    tui_set_bg_rgb(fd, bg);
                    if (idx == 0) {
                        /* 空单元：仅背景色，无字符 */
                        tui_set_fg_rgb(fd, empty);
                        write(fd, " ", 1);
                    } else {
                        /* 着色字符：dim -> base 渐变 */
                        tui_rgb_t c1 = v3_mix(dim, base, v);
                        tui_set_fg_rgb(fd, c1);
                        tui_set_attr(fd, TUI_ATTR_BOLD);
                        write(fd, lvl[idx], 3);
                    }
                    tui_reset_style(fd);
                    tui_set_bg_rgb(fd, bg);
                }
            }
        }
    }

    return TUI_OK;
}

tui_layout_t *tui_heatmap_new(const double *data, int rows, int cols,
                              const char **row_labels)
{
    tui_heatmap_t *hm = (tui_heatmap_t *)calloc(1, sizeof(tui_heatmap_t));
    if (!hm) return NULL;
    hm->data = data;
    hm->rows = rows;
    hm->cols = cols;
    hm->cell_h = 1;
    hm->row_labels = row_labels;
    hm->show_row_labels = row_labels != NULL;
    return tui_layout_leaf_with_free(tui_heatmap_render, hm, free);
}

/* ══════════════════════════════════════════════════════
 *  BarChart 条形图（横向 / 纵向）
 *  (struct tui_barchart_t 在 libtui.h 中定义)
 * ══════════════════════════════════════════════════════ */

int tui_barchart_render(int fd, const tui_rect_t *area, void *userdata)
{
    tui_barchart_t *bc = (tui_barchart_t *)userdata;
    if (!bc || !area) return TUI_ERR_PARAM;
    if (!tui_rect_valid(area) || bc->n <= 0) return TUI_OK;

    tui_rgb_t bg = (bc->bg.r == 0 && bc->bg.g == 0 && bc->bg.b == 0)
        ? v3_surf_bg() : bc->bg;
    tui_rgb_t fg = bc->fg;
    tui_rgb_t dim = v3_dim();
    tui_rgb_t fg2 = v3_grad(3);

    /* 背景 */
    for (int r = 0; r < area->rows; r++) {
        tui_cursor_goto(fd, area->row + r, area->col);
        tui_set_bg_rgb(fd, bg);
        tui_spaces(fd, area->cols);
    }
    tui_reset_style(fd);
    tui_set_bg_rgb(fd, bg);

    if (bc->vertical) {
        /* 纵向：每条占 1 列 */
        int n = bc->n < area->cols ? bc->n : area->cols;
        int start_x = area->col + (area->cols - n) / 2;
        int bottom_y = area->row + area->rows - 1;
        for (int i = 0; i < n; i++) {
            double v = bc->values[i];
            if (v < 0) v = 0;
            if (v > 1) v = 1;
            int h = (int)(v * (area->rows - 1));
            if (h < 0) h = 0;
            if (h > area->rows - 1) h = area->rows - 1;

            /* 渐变填充 */
            for (int j = 0; j < h; j++) {
                double ratio = (double)j / (double)(area->rows - 1);
                tui_rgb_t c = v3_mix(dim, fg, ratio);
                /* c is already clamped to uint8_t range by struct */
                tui_cursor_goto(fd, bottom_y - j, start_x + i);
                tui_set_bg_rgb(fd, bg);
                tui_set_fg_rgb(fd, c);
                tui_set_attr(fd, TUI_ATTR_BOLD);
                write(fd, "█", 3);
                tui_reset_style(fd);
                tui_set_bg_rgb(fd, bg);
            }
        }
    } else {
        /* 横向：每条占 1 行 */
        int n = bc->n < area->rows ? bc->n : area->rows;
        int start_y = area->row + (area->rows - n) / 2;
        int label_w = 0;
        for (int i = 0; i < n; i++) {
            if (bc->labels[i]) {
                int lw = tui_strwidth(bc->labels[i]);
                if (lw > label_w) label_w = lw;
            }
        }
        if (label_w > 12) label_w = 12;
        int bar_w = area->cols - label_w - 8;  /* 留 8 列给数值 */
        if (bar_w < 4) bar_w = 4;

        for (int i = 0; i < n; i++) {
            int y = start_y + i;
            double v = bc->values[i];
            if (v < 0) v = 0;
            if (v > 1) v = 1;
            int filled = (int)(v * bar_w);
            if (filled < 0) filled = 0;
            if (filled > bar_w) filled = bar_w;

            /* 标签 */
            if (bc->labels[i] && label_w > 0) {
                tui_cursor_goto(fd, y, area->col);
                tui_set_bg_rgb(fd, bg);
                tui_set_fg_rgb(fd, dim);
                tui_set_attr(fd, TUI_ATTR_DIM);
                int bytes = tui_truncate(bc->labels[i], label_w);
                write(fd, bc->labels[i], (size_t)bytes);
                int pad = label_w - tui_strwidth(bc->labels[i]);
                for (int p = 0; p < pad; p++) write(fd, " ", 1);
                tui_write(fd, " ");
                tui_reset_style(fd);
                tui_set_bg_rgb(fd, bg);
            }

            /* 条形轨道：bg 与 dim 之间过渡（自然融合卡片底色） */
            tui_rgb_t track = {
                (uint8_t)(bg.r * 0.7 + dim.r * 0.3),
                (uint8_t)(bg.g * 0.7 + dim.g * 0.3),
                (uint8_t)(bg.b * 0.7 + dim.b * 0.3),
            };
            for (int j = 0; j < bar_w; j++) {
                tui_cursor_goto(fd, y, area->col + label_w + 1 + j);
                tui_set_bg_rgb(fd, bg);
                tui_set_fg_rgb(fd, track);
                tui_set_attr(fd, TUI_ATTR_DIM);
                write(fd, "━", 3);
                tui_reset_style(fd);
                tui_set_bg_rgb(fd, bg);
            }

            /* 条形填充：渐变高亮 */
            for (int j = 0; j < filled; j++) {
                double ratio = (double)j / (double)(bar_w > 0 ? bar_w : 1);
                tui_rgb_t c = v3_mix(fg2, fg, ratio);
                tui_cursor_goto(fd, y, area->col + label_w + 1 + j);
                tui_set_bg_rgb(fd, bg);
                tui_set_fg_rgb(fd, c);
                tui_set_attr(fd, TUI_ATTR_BOLD);
                write(fd, "━", 3);
                tui_reset_style(fd);
                tui_set_bg_rgb(fd, bg);
            }

            /* 数值 */
            char vs[8];
            snprintf(vs, sizeof(vs), "%3d%%", (int)(v * 100));
            tui_cursor_goto(fd, y, area->col + area->cols - 4);
            tui_set_bg_rgb(fd, bg);
            tui_set_fg_rgb(fd, fg);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            tui_write(fd, vs);
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, bg);
        }
    }

    return TUI_OK;
}

tui_layout_t *tui_barchart_new(const char **labels, const double *values,
                               int n, int vertical, tui_rgb_t fg)
{
    tui_barchart_t *bc = (tui_barchart_t *)calloc(1, sizeof(tui_barchart_t));
    if (!bc) return NULL;
    bc->labels = labels;
    bc->values = values;
    bc->n = n;
    bc->vertical = vertical;
    bc->fg = fg;
    return tui_layout_leaf_with_free(tui_barchart_render, bc, free);
}

/* ══════════════════════════════════════════════════════
 *  Marquee 跑马灯 / 滚动字幕
 *
 *  frame: 起始偏移（每帧 +1）
 *  text: 长文本
 *  width: 可见宽度
 *  (struct tui_marquee_t 在 libtui.h 中定义)
 * ══════════════════════════════════════════════════════ */

int tui_marquee_render(int fd, const tui_rect_t *area, void *userdata)
{
    tui_marquee_t *m = (tui_marquee_t *)userdata;
    if (!m || !area || !m->text) return TUI_ERR_PARAM;
    if (!tui_rect_valid(area)) return TUI_OK;

    tui_rgb_t bg = (m->bg.r == 0 && m->bg.g == 0 && m->bg.b == 0)
        ? v3_surf_bg() : m->bg;

    int w = area->cols;
    if (w < 1) return TUI_OK;

    /* 背景 */
    tui_cursor_goto(fd, area->row, area->col);
    tui_set_bg_rgb(fd, bg);
    tui_spaces(fd, w);
    tui_reset_style(fd);
    tui_set_bg_rgb(fd, bg);

    int text_w = tui_strwidth(m->text);
    if (text_w == 0) return TUI_OK;

    /* 拼接文本以实现无缝循环："text   text" */
    int gap = 4;
    int cycle = text_w + gap;
    int offset = m->frame % cycle;
    if (offset < 0) offset += cycle;

    /* 构造显示缓冲 */
    char buf[512];
    int dst = 0;

    /* 重复 text 一份 */
    char *extended = malloc(cycle * 2 + 1);
    if (!extended) return TUI_OK;
    snprintf(extended, cycle * 2 + 1, "%s%*s", m->text, gap, "");
    /* 再补一份 */
    strncat(extended, m->text, cycle);

    /* 从 offset 开始取 width 列 */
    int src = offset;
    while (extended[src] && dst < w) {
        unsigned char c = (unsigned char)extended[src];
        int bytes;
        if (c < 0x80) bytes = 1;
        else if (c < 0xE0) bytes = 2;
        else if (c < 0xF0) bytes = 3;
        else bytes = 4;
        int wch = (bytes >= 3) ? 2 : 1;
        if (dst + wch > w) break;
        memcpy(buf + dst, extended + src, bytes);
        dst += wch;
        src += bytes;
        /* 防止无限循环 */
        if (src > cycle * 2) break;
    }
    free(extended);

    tui_cursor_goto(fd, area->row, area->col);
    tui_set_bg_rgb(fd, bg);
    tui_set_fg_rgb(fd, m->fg);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    if (dst > 0) write(fd, buf, (size_t)dst);
    tui_reset_style(fd);
    return TUI_OK;
}

void tui_marquee_tick(tui_marquee_t *m)
{
    if (m) m->frame++;
}

tui_layout_t *tui_marquee_new(const char *text, tui_rgb_t fg)
{
    tui_marquee_t *m = (tui_marquee_t *)calloc(1, sizeof(tui_marquee_t));
    if (!m) return NULL;
    m->text = text;
    m->fg = fg;
    m->frame = 0;
    m->speed = 1;
    return tui_layout_leaf_with_free(tui_marquee_render, m, free);
}

/* ══════════════════════════════════════════════════════
 *  Tree 树形结构
 *
 *  适合文件树 / 层级菜单 / 思维导图
 *  (struct 在 libtui.h 中定义)
 * ══════════════════════════════════════════════════════ */

int tui_tree_render(int fd, const tui_rect_t *area, void *userdata)
{
    tui_tree_t *t = (tui_tree_t *)userdata;
    if (!t || !area) return TUI_ERR_PARAM;
    if (!tui_rect_valid(area) || t->n <= 0) return TUI_OK;

    tui_rgb_t bg = (t->bg.r == 0 && t->bg.g == 0 && t->bg.b == 0)
        ? v3_surf_bg() : t->bg;
    tui_rgb_t fg = t->fg;
    tui_rgb_t dim = v3_dim();
    tui_rgb_t accent = v3_grad(3);

    /* 背景 */
    for (int r = 0; r < area->rows; r++) {
        tui_cursor_goto(fd, area->row + r, area->col);
        tui_set_bg_rgb(fd, bg);
        tui_spaces(fd, area->cols);
    }
    tui_reset_style(fd);
    tui_set_bg_rgb(fd, bg);

    int max_lines = area->rows;
    for (int i = 0; i < t->n && i < max_lines; i++) {
        tui_tree_node_t *n = &t->nodes[i];
        int y = area->row + i;
        int x = area->col;
        int is_sel = (i == t->selected);

        /* 关键：每行重置光标到行首 */
        tui_cursor_goto(fd, y, x);
        tui_set_bg_rgb(fd, is_sel ? v3_grad(5) : bg);

        /* 选中行高亮背景 */
        if (is_sel) {
            tui_spaces(fd, area->cols);
            tui_cursor_goto(fd, y, x);
            tui_set_bg_rgb(fd, v3_grad(5));
        }

        /* 缩进 */
        int indent = n->depth * 2;
        if (indent > 8) indent = 8;
        for (int p = 0; p < indent; p++) {
            tui_set_fg_rgb(fd, dim);
            tui_write(fd, " ");
        }

        /* 节点符号 */
        const char *sym = "  ";
        if (!n->is_leaf) {
            if (n->expanded) sym = "▾ ";
            else             sym = "▸ ";
        } else {
            sym = "  ";
        }
        tui_set_fg_rgb(fd, is_sel ? v3_surf_fg() : accent);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, sym);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, is_sel ? v3_grad(5) : bg);

        /* 标签 */
        if (n->label) {
            tui_set_fg_rgb(fd, is_sel ? v3_surf_fg() : fg);
            tui_set_attr(fd, is_sel ? TUI_ATTR_BOLD : TUI_ATTR_RESET);
            int avail = area->cols - indent - 2;
            int bytes = tui_truncate(n->label, avail);
            if (bytes > 0) write(fd, n->label, (size_t)bytes);
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, is_sel ? v3_grad(5) : bg);
        }
    }

    return TUI_OK;
}

tui_layout_t *tui_tree_new(tui_tree_node_t *nodes, int n, int selected,
                           tui_rgb_t fg)
{
    tui_tree_t *t = (tui_tree_t *)calloc(1, sizeof(tui_tree_t));
    if (!t) return NULL;
    t->nodes = nodes;
    t->n = n;
    t->selected = selected;
    t->fg = fg;
    return tui_layout_leaf_with_free(tui_tree_render, t, free);
}

/* ══════════════════════════════════════════════════════
 *  ActivityLog 活动日志流
 *
 *  时间戳 + 等级 + 消息，自动滚动到最新
 *  (struct tui_log_entry_t / tui_log_t 在 libtui.h 中定义)
 * ══════════════════════════════════════════════════════ */

int tui_activitylog_render(int fd, const tui_rect_t *area, void *userdata)
{
    tui_log_t *l = (tui_log_t *)userdata;
    if (!l || !area) return TUI_ERR_PARAM;
    if (!tui_rect_valid(area) || l->n <= 0) return TUI_OK;

    tui_rgb_t bg = (l->bg.r == 0 && l->bg.g == 0 && l->bg.b == 0)
        ? v3_surf_bg() : l->bg;
    tui_rgb_t dim = v3_dim();
    tui_rgb_t fg = v3_surf_fg();

    /* 背景 */
    for (int r = 0; r < area->rows; r++) {
        tui_cursor_goto(fd, area->row + r, area->col);
        tui_set_bg_rgb(fd, bg);
        tui_spaces(fd, area->cols);
    }
    tui_reset_style(fd);
    tui_set_bg_rgb(fd, bg);

    int vis = area->rows;
    int start = l->n - vis - l->scroll;
    if (start < 0) start = 0;

    for (int i = 0; i < vis; i++) {
        int idx = start + i;
        if (idx >= l->n) break;
        const tui_log_entry_t *e = &l->entries[idx];
        int y = area->row + i;
        int x = area->col;

        /* 时间戳 */
        if (e->time) {
            tui_cursor_goto(fd, y, x);
            tui_set_bg_rgb(fd, bg);
            tui_set_fg_rgb(fd, dim);
            tui_set_attr(fd, TUI_ATTR_DIM);
            tui_write(fd, e->time);
            x += tui_strwidth(e->time) + 1;
            tui_write(fd, " ");
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, bg);
        }

        /* 等级（带色块） */
        if (e->level) {
            int lvl_w = tui_strwidth(e->level) + 2;
            tui_cursor_goto(fd, y, x);
            tui_set_bg_rgb(fd, e->level_fg);
            tui_set_fg_rgb(fd, bg);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            tui_write(fd, " ");
            tui_write(fd, e->level);
            tui_write(fd, " ");
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, bg);
            x += lvl_w + 1;
        }

        /* 消息 */
        if (e->message) {
            tui_cursor_goto(fd, y, x);
            tui_set_bg_rgb(fd, bg);
            tui_set_fg_rgb(fd, fg);
            tui_set_attr(fd, TUI_ATTR_RESET);
            int avail = area->cols - (x - area->col);
            int bytes = tui_truncate(e->message, avail);
            if (bytes > 0) write(fd, e->message, (size_t)bytes);
            tui_reset_style(fd);
        }
    }
    return TUI_OK;
}

tui_layout_t *tui_activitylog_new(const tui_log_entry_t *entries, int n)
{
    tui_log_t *l = (tui_log_t *)calloc(1, sizeof(tui_log_t));
    if (!l) return NULL;
    l->entries = entries;
    l->n = n;
    l->scroll = 0;
    return tui_layout_leaf_with_free(tui_activitylog_render, l, free);
}

/* ══════════════════════════════════════════════════════
 *  Pulse 脉动指示器
 *
 *  根据 frame 状态变化显示 ●/○/◐/◑/◒/◓ 旋转效果
 *  (struct tui_pulse_t 在 libtui.h 中定义)
 * ══════════════════════════════════════════════════════ */

int tui_pulse_render(int fd, const tui_rect_t *area, void *userdata)
{
    tui_pulse_t *p = (tui_pulse_t *)userdata;
    if (!p || !area) return TUI_ERR_PARAM;
    if (!tui_rect_valid(area)) return TUI_OK;

    tui_rgb_t bg = (p->bg.r == 0 && p->bg.g == 0 && p->bg.b == 0)
        ? v3_surf_bg() : p->bg;

    /* 8 帧旋转 */
    static const char *pulse_chars[8] = {"◐", "◓", "◑", "◒", "◐", "◓", "◑", "◒"};
    int idx = p->frame % 8;
    if (idx < 0) idx = -idx;

    tui_cursor_goto(fd, area->row, area->col);
    tui_set_bg_rgb(fd, bg);
    tui_set_fg_rgb(fd, p->fg);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    write(fd, pulse_chars[idx], 3);
    tui_reset_style(fd);
    tui_set_bg_rgb(fd, bg);

    if (p->label && p->label[0]) {
        tui_set_fg_rgb(fd, v3_surf_fg());
        tui_set_attr(fd, TUI_ATTR_RESET);
        tui_write(fd, " ");
        tui_write(fd, p->label);
        tui_reset_style(fd);
    }
    return TUI_OK;
}

void tui_pulse_tick(tui_pulse_t *p)
{
    if (p) p->frame++;
}

tui_layout_t *tui_pulse_new(tui_rgb_t fg, const char *label)
{
    tui_pulse_t *p = (tui_pulse_t *)calloc(1, sizeof(tui_pulse_t));
    if (!p) return NULL;
    p->fg = fg;
    p->label = label;
    p->frame = 0;
    return tui_layout_leaf_with_free(tui_pulse_render, p, free);
}

/* ══════════════════════════════════════════════════════
 *  Box 装饰盒（带标题、5 种风格）
 *  (enum TUI_BOX_* / struct tui_box_t 在 libtui.h 中定义)
 * ══════════════════════════════════════════════════════ */

int tui_box_render(int fd, const tui_rect_t *area, void *userdata)
{
    tui_box_t *b = (tui_box_t *)userdata;
    if (!b || !area) return TUI_ERR_PARAM;
    if (!tui_rect_valid(area)) return TUI_OK;

    if (area->rows < 2 || area->cols < 4) return TUI_OK;

    tui_rgb_t bg = (b->bg.r == 0 && b->bg.g == 0 && b->bg.b == 0)
        ? v3_surf_bg() : b->bg;
    tui_rgb_t border = b->border;
    tui_rgb_t title_fg = b->title_fg;

    static const char *box_chars[5][6] = {
        /* THIN  */ {"─", "│", "┌", "┐", "└", "┘"},
        /* BOLD  */ {"━", "┃", "┏", "┓", "┗", "┛"},
        /* DBL   */ {"═", "║", "╔", "╗", "╚", "╝"},
        /* RND   */ {"─", "│", "╭", "╮", "╰", "╯"},
        /* ASCII */ {"-", "|", "+", "+", "+", "+"},
    };
    int st = b->style;
    if (st < 0 || st > 4) st = 0;
    const char **bc = box_chars[st];

    /* 背景 */
    for (int r = 0; r < area->rows; r++) {
        tui_cursor_goto(fd, area->row + r, area->col);
        tui_set_bg_rgb(fd, bg);
        tui_spaces(fd, area->cols);
    }
    tui_reset_style(fd);
    tui_set_bg_rgb(fd, bg);

    int w = area->cols;
    int h = area->rows;

    /* 上下边框 */
    for (int cx = 0; cx < w; cx++) {
        tui_cursor_goto(fd, area->row, area->col + cx);
        tui_set_bg_rgb(fd, bg);
        tui_set_fg_rgb(fd, border);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        if (cx == 0) write(fd, bc[2], 3);
        else if (cx == w - 1) write(fd, bc[3], 3);
        else write(fd, bc[0], 3);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, bg);

        if (h > 1) {
            tui_cursor_goto(fd, area->row + h - 1, area->col + cx);
            tui_set_bg_rgb(fd, bg);
            tui_set_fg_rgb(fd, border);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            if (cx == 0) write(fd, bc[4], 3);
            else if (cx == w - 1) write(fd, bc[5], 3);
            else write(fd, bc[0], 3);
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, bg);
        }
    }

    /* 左右边框 */
    for (int cy = 1; cy < h - 1; cy++) {
        tui_cursor_goto(fd, area->row + cy, area->col);
        tui_set_bg_rgb(fd, bg);
        tui_set_fg_rgb(fd, border);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        write(fd, bc[1], 3);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, bg);

        if (w > 1) {
            tui_cursor_goto(fd, area->row + cy, area->col + w - 1);
            tui_set_bg_rgb(fd, bg);
            tui_set_fg_rgb(fd, border);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            write(fd, bc[1], 3);
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, bg);
        }
    }

    /* 标题（顶行居中，浮在边框上） */
    if (b->title[0] && w >= 8) {
        char title_buf[80];
        int tlen = tui_strwidth(b->title);
        snprintf(title_buf, sizeof(title_buf), " %s ", b->title);
        int full_w = tlen + 2;
        if (full_w < w - 4) {
            int start = area->col + (w - full_w) / 2;
            tui_cursor_goto(fd, area->row, start);
            tui_set_bg_rgb(fd, bg);
            tui_set_fg_rgb(fd, title_fg);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            tui_write(fd, title_buf);
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, bg);
        }
    }

    /* 内容区 */
    if (b->content_fn && h >= 3 && w >= 4) {
        tui_rect_t inner = {
            area->row + 1, area->col + 1, h - 2, w - 2
        };
        b->content_fn(fd, &inner, b->content_data);
    }

    return TUI_OK;
}

tui_layout_t *tui_box_new(const char *title, int style, tui_render_fn fn,
                          void *data)
{
    tui_box_t *b = (tui_box_t *)calloc(1, sizeof(tui_box_t));
    if (!b) return NULL;
    if (title) strncpy(b->title, title, sizeof(b->title) - 1);
    b->style = style;
    b->content_fn = fn;
    b->content_data = data;
    return tui_layout_leaf_with_free(tui_box_render, b, free);
}
