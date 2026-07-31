/* demo_showcase.c — meuos-libtui V3 组件展示
 *
 * 集中演示所有 viz 组件:
 *   - Banner / Marquee / Pulse
 *   - Stat cards (大数字)
 *   - Gauge (4 风格)
 *   - Heatmap
 *   - BarChart
 *   - Tree
 *   - ActivityLog
 *   - Spinner
 *
 * 一次性渲染模式: TUI_DEMO_CAPTURE=1
 * 主题选择: TUI_DEMO_THEME=<name> 或 argv[1]
 */

#define _XOPEN_SOURCE 700

#include "meuos/libtui.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/* ── 数据 ─────────────────────────────────────────── */

static const int cpu_data_24[24] = {
    42, 48, 51, 47, 53, 60, 58, 55,
    49, 52, 67, 71, 65, 62, 58, 60,
    64, 70, 75, 72, 68, 63, 57, 52
};

static const int mem_data_24[24] = {
    35, 38, 42, 40, 45, 50, 55, 60,
    58, 55, 52, 50, 48, 45, 43, 42,
    45, 48, 52, 58, 62, 60, 55, 48
};

static const int net_data_24[24] = {
    12, 18, 25, 22, 30, 45, 60, 75,
    82, 70, 65, 58, 50, 45, 55, 68,
    75, 80, 85, 78, 65, 50, 35, 22
};

static const int disk_data_24[24] = {
    5, 5, 5, 5, 5, 8, 12, 15,
    18, 15, 12, 10, 8, 8, 10, 15,
    20, 25, 22, 18, 15, 12, 8, 5
};

/* 7 天 × 24 小时 CPU 占用热力图 (0.0-1.0) */
static const double heatmap_data[7][24] = {
    { 0.1, 0.2, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.6, 0.5,
      0.4, 0.3, 0.3, 0.4, 0.5, 0.6, 0.7, 0.5, 0.4, 0.3, 0.2, 0.1 },
    { 0.2, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 0.7, 0.6,
      0.5, 0.4, 0.4, 0.5, 0.6, 0.7, 0.8, 0.6, 0.5, 0.4, 0.3, 0.2 },
    { 0.3, 0.2, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 0.7,
      0.6, 0.5, 0.4, 0.4, 0.5, 0.6, 0.7, 0.8, 0.6, 0.5, 0.4, 0.3 },
    { 0.4, 0.3, 0.2, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9,
      0.7, 0.6, 0.5, 0.4, 0.4, 0.5, 0.6, 0.7, 0.8, 0.6, 0.5, 0.4 },
    { 0.5, 0.4, 0.3, 0.2, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8,
      0.9, 0.7, 0.6, 0.5, 0.4, 0.4, 0.5, 0.6, 0.7, 0.8, 0.6, 0.5 },
    { 0.3, 0.4, 0.5, 0.4, 0.3, 0.2, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6,
      0.7, 0.8, 0.9, 0.7, 0.6, 0.5, 0.4, 0.4, 0.5, 0.6, 0.5, 0.3 },
    { 0.2, 0.3, 0.4, 0.5, 0.4, 0.3, 0.2, 0.1, 0.2, 0.3, 0.4, 0.5,
      0.6, 0.7, 0.6, 0.5, 0.4, 0.3, 0.4, 0.5, 0.4, 0.3, 0.2, 0.1 },
};

static const char *weekday_labels[7] = {
    "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"
};

/* 服务条形图数据 */
static const char *service_labels[5] = {
    "nginx", "redis", "postgres", "meuos", "kafka"
};
static const double service_values[5] = {
    0.85, 0.62, 0.74, 0.93, 0.81
};

/* 文件树 */
static tui_tree_node_t tree_nodes[] = {
    { "src",                       0, 0, 1, 0 },
    { "widgets_viz.c",              1, 1, 0, 0 },
    { "themes.c",                  1, 1, 0, 0 },
    { "layout.c",                  1, 1, 0, 0 },
    { "include",                   0, 0, 1, 0 },
    { "libtui.h",                  1, 1, 0, 1 },
    { "examples",                  0, 0, 0, 0 },
    { "demo_showcase.c",           1, 1, 0, 0 },
    { "scripts",                   0, 0, 0, 0 },
    { "capture-tui.sh",            1, 1, 0, 0 },
};

/* 活动日志 */
static tui_log_entry_t activity_log[] = {
    { "14:32:05", "OK",    {  74, 222, 128 }, "Health check passed" },
    { "14:32:12", "WARN",  { 250, 204,  21 }, "Memory usage 84%" },
    { "14:32:45", "INFO",  {  56, 189, 248 }, "Building artifacts" },
    { "14:33:02", "OK",    {  74, 222, 128 }, "Build complete" },
    { "14:33:18", "ERROR", { 248, 113, 113 }, "Test failed: auth" },
};

/* ── 子内容 ──────────────────────────────────────── */

static tui_layout_t *build_root(int theme_idx);

int main(int argc, char **argv)
{
    int theme_idx = 0;
    const char *env_theme = getenv("TUI_DEMO_THEME");
    if (env_theme && env_theme[0]) {
        const tui_theme_t *t = tui_theme_by_name(env_theme);
        if (t) {
            for (int i = 0; i < tui_themes_count; i++)
                if (tui_themes[i] == t) { theme_idx = i; break; }
        }
    }
    if (argc > 1) {
        const tui_theme_t *t = tui_theme_by_name(argv[1]);
        if (t) {
            for (int i = 0; i < tui_themes_count; i++)
                if (tui_themes[i] == t) { theme_idx = i; break; }
        }
    }

    int ofd = STDOUT_FILENO;
    int ifd = STDIN_FILENO;
    int capture = getenv("TUI_DEMO_CAPTURE") != NULL;
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (!capture) tui_raw_mode(ifd, 1);
    tui_clear_screen(ofd);
    tui_cursor_show(ofd, 0);

    tui_size_t sz;
    if (capture) { sz.rows = 38; sz.cols = 110; }
    else if (tui_get_size(ofd, &sz) != TUI_OK) { sz.rows = 38; sz.cols = 110; }

    tui_layout_t *root = build_root(theme_idx);
    if (root) {
        tui_rect_t area = { 1, 1, sz.rows, sz.cols - 1 };
        /* 用 surface_bg 铺满整个画布，避免空隙区域显示终端默认黑色 */
        {
            const tui_theme_t *th_bg = tui_theme_current();
            tui_rgb_t surf_bg = th_bg ? th_bg->surface_bg : (tui_rgb_t){13,13,23};
            for (int r = 0; r < area.rows; r++) {
                tui_cursor_goto(ofd, area.row + r, area.col);
                tui_set_bg_rgb(ofd, surf_bg);
                tui_spaces(ofd, area.cols);
            }
            tui_reset_style(ofd);
            tui_cursor_goto(ofd, area.row, area.col);
        }
        tui_layout_render(ofd, root, area);
        tui_layout_free(root);
    }
    fflush(stdout);
    fsync(ofd);
    if (capture) return 0;

    tui_event_t ev;
    while (1) {
        tui_getkey(ifd, &ev);
        if (ev.key == TUI_KEY_ESC || (ev.key >= 0x20 && ev.key < 0x7F && (char)ev.key == 'q'))
            break;
        if (ev.key >= '1' && ev.key <= '6') {
            theme_idx = (int)ev.key - '1';
            tui_clear_screen(ofd);
            tui_layout_t *r = build_root(theme_idx);
            if (r) {
                tui_rect_t area = { 1, 1, sz.rows, sz.cols - 1 };
                /* 同上：铺 surface_bg 底色 */
                {
                    const tui_theme_t *th_bg = tui_theme_current();
                    tui_rgb_t surf_bg = th_bg ? th_bg->surface_bg : (tui_rgb_t){13,13,23};
                    for (int r2 = 0; r2 < area.rows; r2++) {
                        tui_cursor_goto(ofd, area.row + r2, area.col);
                        tui_set_bg_rgb(ofd, surf_bg);
                        tui_spaces(ofd, area.cols);
                    }
                    tui_reset_style(ofd);
                    tui_cursor_goto(ofd, area.row, area.col);
                }
                tui_layout_render(ofd, r, area);
                tui_layout_free(r);
            }
            fflush(stdout);
        }
    }
    tui_cursor_show(ofd, 1);
    tui_clear_screen(ofd);
    tui_raw_mode(ifd, 0);
    return 0;
}

/* ══════════════════════════════════════════════════════
 *  头栏：Logo + 标题 + 主题 chip + 时间戳
 * ══════════════════════════════════════════════════════ */

static int header_render(int fd, const tui_rect_t *area, void *udata)
{
    (void)udata;
    const tui_theme_t *th = tui_theme_current();
    if (!th) th = &tui_theme_meuos;
    tui_rgb_t hbg = th->header_bg;
    tui_rgb_t hfg = th->header_fg;
    tui_rgb_t dim = (tui_rgb_t){ 110, 115, 130 };

    int y = area->row, x = area->col, w = area->cols;

    /* 整行底色 */
    tui_cursor_goto(fd, y, x);
    tui_set_bg_rgb(fd, hbg);
    tui_spaces(fd, w);
    tui_reset_style(fd);

    /* 左：Logo + 渐变标题 */
    tui_cursor_goto(fd, y, x);
    tui_set_bg_rgb(fd, hbg);
    tui_set_fg_rgb(fd, th->gradient[4]);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_write(fd, " ▸");
    tui_reset_style(fd);
    tui_set_bg_rgb(fd, hbg);

    const char *title = "meuos-libtui";
    tui_cursor_goto(fd, y, x + 4);
    tui_set_fg_rgb(fd, hfg);
    tui_set_bg_rgb(fd, hbg);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_write(fd, title);
    tui_reset_style(fd);
    tui_set_bg_rgb(fd, hbg);

    tui_set_fg_rgb(fd, dim);
    tui_set_attr(fd, TUI_ATTR_DIM);
    tui_write(fd, "  Showcase  ");
    tui_reset_style(fd);
    tui_set_bg_rgb(fd, hbg);

    /* 右：主题 chip + 时间戳 */
    int chip_w = (int)strlen(th->name) + 2;
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char ts[16];
    strftime(ts, sizeof(ts), "%H:%M:%S", &tmv);
    int ts_w = 8;
    int right_total = chip_w + 1 + ts_w + 2;
    int start = x + w - right_total;

    tui_cursor_goto(fd, y, start);
    tui_set_bg_rgb(fd, th->gradient[4]);
    tui_set_fg_rgb(fd, hbg);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_write(fd, " ");
    tui_write(fd, th->name);
    tui_write(fd, " ");
    tui_reset_style(fd);
    tui_set_bg_rgb(fd, hbg);

    tui_cursor_goto(fd, y, start + chip_w + 1);
    tui_set_bg_rgb(fd, hbg);
    tui_set_fg_rgb(fd, hfg);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_write(fd, ts);
    tui_reset_style(fd);
    return TUI_OK;
}

/* ══════════════════════════════════════════════════════
 *  跑马灯行
 * ══════════════════════════════════════════════════════ */

static int marquee_render(int fd, const tui_rect_t *area, void *udata)
{
    (void)udata;
    const tui_theme_t *th = tui_theme_current();
    tui_rgb_t bg = th->surface_bg;
    tui_rgb_t dim = (tui_rgb_t){ 120, 125, 145 };

    int y = area->row, x = area->col, w = area->cols;

    /* 顶部 1px 高亮 + 底部 1px 高亮 - 装饰条 */
    if (area->rows >= 1) {
        tui_cursor_goto(fd, y, x);
        tui_set_bg_rgb(fd, bg);
        tui_spaces(fd, w);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, bg);
    }

    if (w < 4) return TUI_OK;
    if (area->rows < 1) return TUI_OK;

    int tx = x;
    /* 左侧 ▸ icon (用渐变色) */
    tui_cursor_goto(fd, y, tx);
    tui_set_bg_rgb(fd, bg);
    tui_set_fg_rgb(fd, th->gradient[3]);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_write(fd, "▶");
    tui_reset_style(fd);
    tui_set_bg_rgb(fd, bg);
    tx += 2;

    /* 文字：使用粗体彩色 */
    tui_cursor_goto(fd, y, tx);
    tui_set_bg_rgb(fd, bg);
    tui_set_fg_rgb(fd, th->gradient[4]);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_write(fd, "A modern TUI framework");
    tui_set_fg_rgb(fd, dim);
    tui_set_attr(fd, TUI_ATTR_RESET);
    tui_write(fd, "  ·  ");
    tui_set_fg_rgb(fd, th->gradient[1]);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_write(fd, "C11");
    tui_set_fg_rgb(fd, dim);
    tui_set_attr(fd, TUI_ATTR_RESET);
    tui_write(fd, "  ·  ");
    tui_set_fg_rgb(fd, th->gradient[2]);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_write(fd, "24-bit color");
    tui_set_fg_rgb(fd, dim);
    tui_set_attr(fd, TUI_ATTR_RESET);
    tui_write(fd, "  ·  ");
    tui_set_fg_rgb(fd, th->gradient[5]);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_write(fd, "Zero deps");
    tui_set_fg_rgb(fd, dim);
    tui_set_attr(fd, TUI_ATTR_RESET);
    tui_write(fd, "  ·  ");
    tui_set_fg_rgb(fd, th->gradient[3]);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_write(fd, "Animated");
    tui_set_fg_rgb(fd, dim);
    tui_set_attr(fd, TUI_ATTR_RESET);
    tui_write(fd, "  ·  ");
    tui_set_fg_rgb(fd, th->gradient[4]);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_write(fd, "MIT");
    tui_reset_style(fd);
    tui_set_bg_rgb(fd, bg);
    return TUI_OK;
}

/* ══════════════════════════════════════════════════════
 *  4 个 Stat 卡（横向排列）
 * ══════════════════════════════════════════════════════ */

/* 卡片数据：value + 进度值 + 单位 + sparkline */
typedef struct {
    const char  *label;
    const char  *value;       /* 主数字 (e.g. "67", "4.2", "1.2", "85") */
    const char  *unit;        /* 单位 (e.g. "%", "G", "G", "MB/s") */
    double       progress;    /* 0.0-1.0 进度条 */
    tui_rgb_t    color;       /* 主题色 */
    int          trend;       /* -1/0/+1 */
    const char  *delta;       /* 变化量 */
    const int   *spark_data;
    int          spark_n;
} big_stat_data_t;

/* 趋势字符 */
static const char *trend_arrow(int trend, char *buf)
{
    if (trend > 0)      { buf[0] = '<'; buf[1] = 0; return buf; }
    else if (trend < 0) { buf[0] = '>'; buf[1] = 0; return buf; }
    else                { buf[0] = '='; buf[1] = 0; return buf; }
}

/* 渲染单个 BigStat 卡片：5 (bignum) + 1 (bar fill) + 1 (sparkline) = 7 行
 *  不再保留 icon/trend 行（标签已在 box 标题里），也不再有 progress track 灰底 */
static int big_stat_render(int fd, const tui_rect_t *area, void *udata)
{
    big_stat_data_t *d = (big_stat_data_t *)udata;
    if (!d || !area) return TUI_ERR_PARAM;
    if (!tui_rect_valid(area)) return TUI_OK;

    const tui_theme_t *th = tui_theme_current();
    tui_rgb_t bg = th->surface_bg;
    tui_rgb_t dim = (tui_rgb_t){ 100, 100, 120 };

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
    tui_set_bg_rgb(fd, bg);

    if (h < 7 || w < 12) return TUI_OK;

    /* ── Rows 0-4: BIG NUMBER (3 列 × 5 行 数字时钟) ── */
    tui_bignum_render_at(fd, y, x, d->color, bg, d->value);

    /* 右上：趋势 + delta 紧凑显示（占用 BigNum 第 0 行右侧，不占额外行） */
    if (d->delta && d->delta[0]) {
        tui_rgb_t tc = dim;
        const char *arrow = "─";
        int arrow_w = 1;
        if (d->trend > 0)      { arrow = "▲"; tc = th->gradient[3]; }
        else if (d->trend < 0) { arrow = "▼"; tc = (tui_rgb_t){248, 113, 113}; }
        else                   { arrow = "─"; tc = dim; }

        int dl = (int)strlen(d->delta);
        int tlen = arrow_w + 1 + dl;
        if (x + w - tlen > x + 8) {  /* 不覆盖 BigNum 区域 */
            tui_cursor_goto(fd, y, x + w - tlen);
            tui_set_bg_rgb(fd, bg);
            tui_set_fg_rgb(fd, tc);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            write(fd, arrow, 3);
            tui_write(fd, " ");
            tui_write(fd, d->delta);
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, bg);
        }
    }

    /* ── Row 5: 进度条填充（无 track 灰底，避免背景色突兀） ──
     * 直接用 ▆ 块覆盖 0..progress 段，未填充段保持卡片底色 bg，
     * 视觉上不会出现与卡片底色不同的灰条。 */
    if (h >= 6) {
        int bar_y = y + 5;
        int bar_w = w;
        int fill_w = (int)(d->progress * bar_w);
        if (fill_w < 0) fill_w = 0;
        if (fill_w > bar_w) fill_w = bar_w;

        tui_cursor_goto(fd, bar_y, x);
        tui_set_bg_rgb(fd, bg);
        for (int i = 0; i < fill_w; i++) {
            double ratio = (double)i / (double)(bar_w > 0 ? bar_w - 1 : 1);
            tui_rgb_t c = {
                (uint8_t)(dim.r + (d->color.r - dim.r) * ratio),
                (uint8_t)(dim.g + (d->color.g - dim.g) * ratio),
                (uint8_t)(dim.b + (d->color.b - dim.b) * ratio),
            };
            tui_set_fg_rgb(fd, c);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            write(fd, "▆", 3);
        }
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, bg);
    }

    /* ── Row 6: sparkline ── */
    if (h >= 7 && d->spark_data && d->spark_n > 0) {
        int sp_y = y + 6;
        static const char *sp_ch[9] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
        int maxv = 100;
        int n = d->spark_n;
        if (n > w) n = w;
        int start = (w - n) / 2;
        if (start < 0) start = 0;

        tui_cursor_goto(fd, sp_y, x);
        tui_set_bg_rgb(fd, bg);
        tui_set_fg_rgb(fd, dim);
        tui_set_attr(fd, TUI_ATTR_DIM);
        for (int i = 0; i < start; i++) tui_write(fd, " ");
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, bg);

        tui_cursor_goto(fd, sp_y, x + start);
        tui_set_bg_rgb(fd, bg);
        for (int i = 0; i < n; i++) {
            int v = d->spark_data[i];
            if (v < 0) v = 0;
            if (v > maxv) v = maxv;
            int idx = (v * 8 + maxv - 1) / maxv;
            if (idx > 8) idx = 8;
            if (idx < 1) idx = 1;

            tui_set_fg_rgb(fd, d->color);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            tui_write(fd, sp_ch[idx]);
        }
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, bg);
    }

    return TUI_OK;
}

/* Sparkline 子回调 */
static int spark_cpu_cb(int fd, const tui_rect_t *area, void *udata)
{
    (void)udata;
    tui_sparkline_t s = {
        .data = cpu_data_24,
        .npoints = 24,
        .max_val = 100,
        .fg = TUI_COLOR_CYAN,
    };
    return tui_sparkline_render(fd, area, &s);
}
static int spark_mem_cb(int fd, const tui_rect_t *area, void *udata)
{
    (void)udata;
    tui_sparkline_t s = {
        .data = mem_data_24,
        .npoints = 24,
        .max_val = 100,
        .fg = TUI_COLOR_GREEN,
    };
    return tui_sparkline_render(fd, area, &s);
}
static int spark_net_cb(int fd, const tui_rect_t *area, void *udata)
{
    (void)udata;
    tui_sparkline_t s = {
        .data = net_data_24,
        .npoints = 24,
        .max_val = 100,
        .fg = TUI_COLOR_MAGENTA,
    };
    return tui_sparkline_render(fd, area, &s);
}
static int spark_disk_cb(int fd, const tui_rect_t *area, void *udata)
{
    (void)udata;
    tui_sparkline_t s = {
        .data = disk_data_24,
        .npoints = 24,
        .max_val = 100,
        .fg = TUI_COLOR_YELLOW,
    };
    return tui_sparkline_render(fd, area, &s);
}

/* 卡片内容 = stat + sparkline */
typedef struct {
    tui_render_fn stat_fn;
    tui_render_fn spark_fn;
} stat_card_data_t;

static int stat_card_content(int fd, const tui_rect_t *area, void *udata)
{
    stat_card_data_t *d = (stat_card_data_t *)udata;
    if (!d) return TUI_ERR_PARAM;

    const tui_theme_t *th = tui_theme_current();
    tui_rgb_t bg = th->surface_bg;

    /* 背景 */
    for (int r = 0; r < area->rows; r++) {
        tui_cursor_goto(fd, area->row + r, area->col);
        tui_set_bg_rgb(fd, bg);
        tui_spaces(fd, area->cols);
    }
    tui_reset_style(fd);
    tui_set_bg_rgb(fd, bg);

    int y = area->row;
    /* 上半: stat (3 rows) */
    tui_rect_t s_area = { y, area->col, 3, area->cols };
    d->stat_fn(fd, &s_area, NULL);

    /* 下半: sparkline (1 row) */
    if (area->rows >= 4) {
        tui_rect_t sp_area = { y + 3, area->col, 1, area->cols };
        d->spark_fn(fd, &sp_area, NULL);
    }

    return TUI_OK;
}

/* ══════════════════════════════════════════════════════
 *  Gauge 行
 * ══════════════════════════════════════════════════════ */

static int gauge_row_render(int fd, const tui_rect_t *area, void *udata)
{
    (void)udata;
    const tui_theme_t *th = tui_theme_current();
    tui_rgb_t bg = th->surface_bg;
    tui_rgb_t dim = (tui_rgb_t){ 100, 100, 120 };

    int y = area->row, x = area->col, w = area->cols, h = area->rows;

    /* 背景 */
    for (int r = 0; r < h; r++) {
        tui_cursor_goto(fd, y + r, x);
        tui_set_bg_rgb(fd, bg);
        tui_spaces(fd, w);
    }
    tui_reset_style(fd);
    tui_set_bg_rgb(fd, bg);

    if (w < 30 || h < 2) return TUI_OK;

    const char *lbls[] = { "LINEAR", "RING", "BAR", "BULLET" };
    tui_rgb_t colors[] = {
        th->gradient[0], th->gradient[2], th->gradient[3], th->gradient[4]
    };
    int n = 4;
    int gap = 2;
    int gw = (w - (n - 1) * gap) / n;
    if (gw < 12) gw = 12;

    for (int i = 0; i < n; i++) {
        int gx = x + i * (gw + gap);
        /* 标签 */
        tui_cursor_goto(fd, y, gx);
        tui_set_bg_rgb(fd, bg);
        tui_set_fg_rgb(fd, dim);
        tui_set_attr(fd, TUI_ATTR_DIM);
        tui_write(fd, lbls[i]);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, bg);

        /* 数值 */
        char vs[8];
        snprintf(vs, sizeof(vs), "%d%%", 20 + i * 18);
        int vw = tui_strwidth(vs);
        tui_cursor_goto(fd, y, gx + gw - vw);
        tui_set_bg_rgb(fd, bg);
        tui_set_fg_rgb(fd, colors[i]);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, vs);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, bg);

        /* gauge */
        tui_rect_t r = { y + 1, gx, h - 1, gw };
        tui_gauge_t g = {
            .label = "",
            .value = 0.20 + i * 0.18,
            .color = colors[i],
            .bg = bg,
            .style = i,
            .ticks = 5,
            .show_value = 0,
        };
        tui_gauge_render(fd, &r, &g);
    }
    return TUI_OK;
}

/* ══════════════════════════════════════════════════════
 *  Heatmap 内容
 * ══════════════════════════════════════════════════════ */

static int heatmap_render(int fd, const tui_rect_t *area, void *udata)
{
    (void)udata;
    tui_heatmap_t hm = {
        .data = &heatmap_data[0][0],
        .rows = 7,
        .cols = 24,
        .cell_h = 1,
        .bg = (tui_rgb_t){0, 0, 0},
        .show_row_labels = 1,
        .row_labels = weekday_labels,
    };
    return tui_heatmap_render(fd, area, &hm);
}

/* ══════════════════════════════════════════════════════
 *  底部三联：BarChart / Tree / ActivityLog
 * ══════════════════════════════════════════════════════ */

static int barchart_render(int fd, const tui_rect_t *area, void *udata)
{
    (void)udata;
    tui_barchart_t bc = {
        .labels = service_labels,
        .values = service_values,
        .n = 5,
        .vertical = 0,
        .fg = tui_theme_current()->gradient[3],
        .bg = (tui_rgb_t){0, 0, 0}
    };
    return tui_barchart_render(fd, area, &bc);
}

static int tree_render(int fd, const tui_rect_t *area, void *udata)
{
    (void)udata;
    tui_tree_t tt = {
        .nodes = tree_nodes,
        .n = sizeof(tree_nodes) / sizeof(tree_nodes[0]),
        .selected = 5,
        .fg = tui_theme_current()->surface_fg,
        .bg = (tui_rgb_t){0, 0, 0}
    };
    return tui_tree_render(fd, area, &tt);
}

static int log_render(int fd, const tui_rect_t *area, void *udata)
{
    (void)udata;
    tui_log_t lg = {
        .entries = activity_log,
        .n = sizeof(activity_log) / sizeof(activity_log[0]),
        .scroll = 0,
        .bg = (tui_rgb_t){0, 0, 0}
    };
    return tui_activitylog_render(fd, area, &lg);
}

/* ══════════════════════════════════════════════════════
 *  主内容区：组织所有组件
 * ══════════════════════════════════════════════════════ */

static int main_content(int fd, const tui_rect_t *area, void *udata)
{
    (void)udata;
    const tui_theme_t *th = tui_theme_current();
    tui_rgb_t surf_bg = th->surface_bg;
    tui_rgb_t accent  = th->gradient[3];
    tui_rgb_t dim     = (tui_rgb_t){ 100, 100, 120 };
    tui_rgb_t fg      = th->surface_fg;

    int y = area->row;
    int x = area->col;
    int w = area->cols;

    /* ── 1. Banner (4 行) ── */
    {
        tui_banner_t *b = calloc(1, sizeof(tui_banner_t));
        snprintf(b->text, sizeof(b->text), "Widget Gallery");
        snprintf(b->sub, sizeof(b->sub), "v3 components · 6 themes · 24-bit color");
        b->color = TUI_COLOR_DEFAULT;
        b->style = TUI_BANNER_DOUBLE;
        b->gradient = 1;
        b->tag = "v3.0";
        tui_rect_t r = { y, x, 4, w };
        tui_banner_render(fd, &r, b);
        free(b);
        y += 4;
    }

    /* ── 2. Stat 卡行 ── */
    {
        int card_h = 9;  /* 1 trend + 3 bignum + 1 bar + 1 sparkline + 1 pad = 7 内行 + 2 框 = 9 外高 */
        int gap = 1;
        int n = 4;
        int cw = (w - (n - 1) * gap) / n;
        if (cw < 22) cw = 22;

        /* 标题条 */
        tui_cursor_goto(fd, y, x);
        tui_set_bg_rgb(fd, surf_bg);
        tui_set_fg_rgb(fd, accent);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, "▸ ");
        tui_set_fg_rgb(fd, fg);
        tui_set_attr(fd, TUI_ATTR_RESET);
        tui_write(fd, "System Metrics");
        tui_set_fg_rgb(fd, dim);
        tui_set_attr(fd, TUI_ATTR_DIM);
        tui_write(fd, "   last 24h");

        /* 右侧 status pill: LIVE */
        tui_cursor_goto(fd, y, x + w - 12);
        tui_set_bg_rgb(fd, th->gradient[2]);
        tui_set_fg_rgb(fd, surf_bg);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, " ");
        tui_write(fd, "●");
        tui_write(fd, " LIVE ");
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, surf_bg);

        /* 4 个 BigStat 卡片 */
        big_stat_data_t stats[] = {
            { "CPU",  "67",   "%",     0.67, th->gradient[1],  1, "+3.2%",  cpu_data_24,  24 },
            { "MEM",  "4.2",  "G",     0.55, th->gradient[3], -1, "-128M",  mem_data_24,  24 },
            { "NET",  "1.2",  "G",     0.42, th->gradient[2],  1, "+12%",   net_data_24,  24 },
            { "DISK", "85",   "MB/s",  0.85, th->gradient[4],  0, "stable", disk_data_24, 24 },
        };
        const char *card_subs[] = { "compute", "memory", "throughput", "iops" };
        tui_rgb_t card_border[] = {
            th->gradient[1], th->gradient[3],
            th->gradient[2], th->gradient[4],
        };
        for (int i = 0; i < n; i++) {
            int cx = x + i * (cw + gap);
            /* 卡片背景（圆角框） */
            tui_box_t box = {0};
            snprintf(box.title, sizeof(box.title), "%s  ·  %s", stats[i].label, card_subs[i]);
            box.title_fg = card_border[i];
            box.border = card_border[i];
            box.bg = surf_bg;
            box.style = TUI_BOX_ROUND;
            tui_rect_t box_r = { y + 1, cx, card_h, cw };
            tui_box_render(fd, &box_r, &box);

            /* 卡片内容（bigstat） */
            tui_rect_t inner = { y + 2, cx + 1, card_h - 2, cw - 2 };
            big_stat_render(fd, &inner, &stats[i]);
        }
        y += card_h + 1;
    }

    /* ── 3. Gauge 行 (5 行含 box) ── */
    {
        int box_h = 5;
        /* box */
        tui_box_t box = {0};
        snprintf(box.title, sizeof(box.title), "✦  Gauge Components");
        box.title_fg = th->gradient[2];
        box.border = th->gradient[2];
        box.bg = surf_bg;
        box.style = TUI_BOX_ROUND;
        tui_rect_t box_r = { y, x, box_h, w };
        tui_box_render(fd, &box_r, &box);

        /* 内容: gauge row */
        tui_rect_t inner = { y + 1, x + 1, box_h - 2, w - 2 };
        gauge_row_render(fd, &inner, NULL);
        y += box_h;
    }

    /* ── 3b. 状态指示行 (1 行) ── */
    {
        /* 实时活动指示器 */
        tui_cursor_goto(fd, y, x);
        tui_set_bg_rgb(fd, surf_bg);
        tui_set_fg_rgb(fd, dim);
        tui_set_attr(fd, TUI_ATTR_DIM);
        tui_write(fd, "  status  ");
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, surf_bg);

        /* 4 个 pulse 指示器 */
        const char *pulse_text[] = { "compiling", "indexing", "deploying", "monitoring" };
        tui_rgb_t pulse_colors[] = {
            th->gradient[3], th->gradient[1],
            th->gradient[4], th->gradient[2],
        };
        int n_p = 4;
        /* 区域总宽: w - 10 (status 标签) - 2 (padding) */
        int avail = w - 12;
        int pw = avail / n_p;
        for (int i = 0; i < n_p; i++) {
            int px = x + 12 + i * pw;

            /* pulse 字符 */
            static const char *pulse_chars[8] = {"◐", "◓", "◑", "◒", "◐", "◓", "◑", "◒"};
            int pframe = i * 2;
            tui_cursor_goto(fd, y, px);
            tui_set_bg_rgb(fd, surf_bg);
            tui_set_fg_rgb(fd, pulse_colors[i]);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            write(fd, pulse_chars[pframe % 8], 3);
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, surf_bg);

            /* 文本 */
            tui_cursor_goto(fd, y, px + 2);
            tui_set_bg_rgb(fd, surf_bg);
            tui_set_fg_rgb(fd, th->surface_fg);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            tui_write(fd, pulse_text[i]);
            tui_set_fg_rgb(fd, dim);
            tui_set_attr(fd, TUI_ATTR_DIM);
            tui_write(fd, "  ▸ ");
            tui_set_fg_rgb(fd, pulse_colors[i]);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            tui_write(fd, "active");
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, surf_bg);
        }
        y += 1;
    }

    /* ── 4. Heatmap 区域 (10 行) ── */
    {
        int box_h = 10;
        tui_box_t box = {0};
        snprintf(box.title, sizeof(box.title), "▸  CPU Heatmap  ·  7d × 24h");
        box.title_fg = th->gradient[1];
        box.border = th->gradient[1];
        box.bg = surf_bg;
        box.style = TUI_BOX_ROUND;
        tui_rect_t box_r = { y, x, box_h, w };
        tui_box_render(fd, &box_r, &box);

        /* 内部：列标签(0/6/12/18/23) + 7 行数据 */
        tui_rect_t inner = { y + 1, x + 1, box_h - 2, w - 2 };

        /* 列标签 (顶部) */
        const char *col_marks = "00  06  12  18  23";
        int label_w = 6;
        tui_cursor_goto(fd, inner.row, inner.col + label_w);
        tui_set_bg_rgb(fd, surf_bg);
        tui_set_fg_rgb(fd, dim);
        tui_set_attr(fd, TUI_ATTR_DIM);
        tui_write(fd, col_marks);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, surf_bg);

        /* heatmap (从第 1 行开始) */
        tui_rect_t hm = { inner.row + 1, inner.col, inner.rows - 1, inner.cols };
        heatmap_render(fd, &hm, NULL);
        y += box_h;
    }

    /* ── 5. 底行三联 (7 行) ── */
    {
        int box_h = 7;
        int log_w = 40;
        int tree_w = 24;
        int gap = 1;
        int bar_w = w - log_w - tree_w - gap * 2;
        if (bar_w < 18) { bar_w = 18; tree_w = (w - bar_w - log_w - gap * 2); }
        if (tree_w < 14) { tree_w = 14; log_w = w - bar_w - tree_w - gap * 2; }

        /* 左: BarChart */
        {
            tui_box_t box = {0};
            snprintf(box.title, sizeof(box.title), "✦  Service Load");
            box.title_fg = th->gradient[4];
            box.border = th->gradient[4];
            box.bg = surf_bg;
            box.style = TUI_BOX_ROUND;
            tui_rect_t box_r = { y, x, box_h, bar_w };
            tui_box_render(fd, &box_r, &box);
            tui_rect_t inner = { y + 1, x + 1, box_h - 2, bar_w - 2 };
            barchart_render(fd, &inner, NULL);
        }
        /* 中: Tree */
        {
            int tx = x + bar_w + gap;
            tui_box_t box = {0};
            snprintf(box.title, sizeof(box.title), "✦  File Tree");
            box.title_fg = th->gradient[5];
            box.border = th->gradient[5];
            box.bg = surf_bg;
            box.style = TUI_BOX_ROUND;
            tui_rect_t box_r = { y, tx, box_h, tree_w };
            tui_box_render(fd, &box_r, &box);
            tui_rect_t inner = { y + 1, tx + 1, box_h - 2, tree_w - 2 };
            tree_render(fd, &inner, NULL);
        }
        /* 右: ActivityLog */
        {
            int lx = x + bar_w + tree_w + gap * 2;
            tui_box_t box = {0};
            snprintf(box.title, sizeof(box.title), "✦  Activity Log");
            box.title_fg = th->gradient[3];
            box.border = th->gradient[3];
            box.bg = surf_bg;
            box.style = TUI_BOX_ROUND;
            tui_rect_t box_r = { y, lx, box_h, log_w };
            tui_box_render(fd, &box_r, &box);
            tui_rect_t inner = { y + 1, lx + 1, box_h - 2, log_w - 2 };
            log_render(fd, &inner, NULL);
        }
        y += box_h;
    }

    return TUI_OK;
}

/* ══════════════════════════════════════════════════════
 *  底部快捷键
 * ══════════════════════════════════════════════════════ */

static tui_keyhint_t demo_hints[6];

static tui_layout_t *build_root(int theme_idx)
{
    tui_set_theme(tui_themes[theme_idx]);

    demo_hints[0] = (tui_keyhint_t){ "1-6", "themes",  TUI_COLOR_DEFAULT };
    demo_hints[1] = (tui_keyhint_t){ "Tab", "scroll",  TUI_COLOR_CYAN    };
    demo_hints[2] = (tui_keyhint_t){ "M",   "marquee", TUI_COLOR_MAGENTA };
    demo_hints[3] = (tui_keyhint_t){ "?",   "help",    TUI_COLOR_GREEN   };
    demo_hints[4] = (tui_keyhint_t){ "Q",   "quit",    TUI_COLOR_RED     };

    /* 根布局: header + marquee + content + footer */
    tui_layout_t *root = tui_layout_vbox(0);

    /* 头栏 (1 行) */
    tui_layout_t *hdr = tui_layout_leaf(header_render, NULL);
    tui_layout_add(root, hdr, 0);

    /* 跑马灯 (1 行) */
    tui_layout_t *mq = tui_layout_leaf(marquee_render, NULL);
    tui_layout_add(root, mq, 0);

    /* 主内容 */
    tui_layout_t *content = tui_layout_leaf(main_content, NULL);
    tui_layout_add(root, content, 1);

    /* 底栏 (固定 2 行：分隔线 + key/label chip) */
    tui_layout_t *kh = tui_keyhints_new(demo_hints, 5);
    tui_layout_add_flex(root, kh, 0, 2);  /* basis=2 = 固定 2 行 */

    return root;
}
