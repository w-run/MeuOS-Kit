/* demo_dashboard.c — meuos-libtui 全特性仪表盘
 *
 * 展示主题切换、Tabs、Stat 卡、Sparkline 折线、Card、KeyHints、
 * 斑马纹表格、Spinner、多种 Progress 风格、Banner 渐变等。
 *
 * 交互:
 *   1-6 切换 6 个主题
 *   ↑↓   切换 tab
 *   q    退出
 *
 * 截图模式 (TUI_DEMO_CAPTURE=1) 渲染一帧后退出。
 */

#define _XOPEN_SOURCE 700

#include "meuos/libtui.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

/* ── 数据 ──────────────────────────────────────────── */

static const int cpu_data[24] = {
    42, 48, 51, 47, 53, 60, 58, 55,
    49, 52, 67, 71, 65, 62, 58, 60,
    64, 70, 75, 72, 68, 63, 57, 52
};

static const int mem_data[12] = {
    60, 62, 65, 63, 67, 70, 68, 71, 74, 72, 75, 78
};

static const int net_data[24] = {
    22, 28, 31, 27, 33, 40, 38, 35,
    29, 32, 47, 51, 45, 42, 38, 40,
    44, 50, 55, 52, 48, 43, 37, 32
};

static const char *processes_cb(int row, int col, void *udata)
{
    (void)udata;
    static const char *data[][4] = {
        { "1",  "systemd",       "12.3",  "RUN"     },
        { "2",  "kthreadd",      "0.0",   "SLEEP"   },
        { "3",  "Xorg",          "8.1",   "RUN"     },
        { "4",  "meuos-tui",     "3.4",   "RUN"     },
        { "5",  "firefox",       "15.7",  "RUN"     },
        { "6",  "gcc",           "22.1",  "RUN"     },
        { "7",  "node",          "5.6",   "SLEEP"   },
    };
    if (row < 7 && col < 4) return data[row][col];
    return "";
}

/* ── 主题辅助：根据 card_bg 决定合适的对比度 ── */

static tui_rgb_t card_bg_color(void)
{
    const tui_theme_t *th = tui_theme_current();
    tui_rgb_t surf_bg = th ? th->surface_bg : (tui_rgb_t){13,13,23};
    /* 卡片背景 = surface + 6（与 stat 卡 / banner / sparkline 面板一致，
     * 否则不同卡片区域会出现深浅不一的"暗色带"） */
    return (tui_rgb_t){
        (uint8_t)(surf_bg.r + 6),
        (uint8_t)(surf_bg.g + 6),
        (uint8_t)(surf_bg.b + 6)
    };
}

static tui_rgb_t row_alt_color(void)
{
    /* 斑马纹偶数行：比 card_bg 再暗 2-3（极轻微可辨，但与 surface 自然过渡） */
    tui_rgb_t c = card_bg_color();
    return (tui_rgb_t){
        (uint8_t)(c.r > 3 ? c.r - 3 : 0),
        (uint8_t)(c.g > 3 ? c.g - 3 : 0),
        (uint8_t)(c.b > 3 ? c.b - 3 : 0)
    };
}

/* ── 子内容回调 ──────────────────────────────────────── */

static int status_card_content(int fd, const tui_rect_t *area, void *udata)
{
    (void)udata;
    const tui_theme_t *th = tui_theme_current();
    tui_rgb_t card_bg = card_bg_color();
    tui_rgb_t surf_fg = th ? th->surface_fg : (tui_rgb_t){229,229,229};
    tui_rgb_t dim     = (tui_rgb_t){110, 115, 130};
    tui_rgb_t ok      = th ? th->gradient[3] : (tui_rgb_t){74,222,128};
    tui_rgb_t warn    = th ? th->gradient[2] : (tui_rgb_t){249,226,175};
    tui_rgb_t info    = th ? th->gradient[2] : (tui_rgb_t){56,189,248};
    tui_rgb_t row_alt = row_alt_color();

    /* 行格式：图标  名称(10)  状态(6)  信息 */
    const struct {
        const char *icon;     /* ●/▲/■ */
        const char *name;     /* build */
        const char *state;    /* OK / RUN / WARN */
        const char *info;     /* 时间/详情 */
        tui_rgb_t  color;     /* 行状态色 */
        int        alt;       /* 是否斑马行 */
    } rows[] = {
        { "●", "build",   "OK",   "00:01:23", ok,      0 },
        { "●", "test",    "OK",   "00:00:42", ok,      1 },
        { "●", "deploy",  "RUN",  "00:00:08", info,    0 },
        { "▲", "cache",   "WARN", "84% full", warn,    1 },
        { "●", "db",      "OK",   "connected",ok,      0 },
        { "●", "monitor", "OK",   "active",   ok,      1 },
    };

    int y = area->row;
    int content_x = area->col;
    int max_x = area->col + area->cols;

    for (int i = 0; i < 6 && y < area->row + area->rows; i++, y++) {
        tui_rgb_t bg = rows[i].alt ? row_alt : card_bg;

        /* 行背景铺满 */
        tui_cursor_goto(fd, y, content_x);
        tui_set_bg_rgb(fd, bg);
        tui_spaces(fd, area->cols);
        tui_reset_style(fd);

        /* 图标 */
        tui_cursor_goto(fd, y, content_x + 1);
        tui_set_bg_rgb(fd, bg);
        tui_set_fg_rgb(fd, rows[i].color);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, rows[i].icon);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, bg);

        /* 名称 */
        tui_cursor_goto(fd, y, content_x + 3);
        tui_set_bg_rgb(fd, bg);
        tui_set_fg_rgb(fd, surf_fg);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, rows[i].name);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, bg);

        /* 状态（左对齐列 14..21） */
        tui_cursor_goto(fd, y, content_x + 14);
        tui_set_bg_rgb(fd, bg);
        tui_set_fg_rgb(fd, rows[i].color);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, rows[i].state);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, bg);

        /* 信息（右对齐到 cols-2） */
        int info_w = tui_strwidth(rows[i].info);
        int info_x = max_x - info_w - 1;
        if (info_x < content_x + 22) info_x = content_x + 22;
        tui_cursor_goto(fd, y, info_x);
        tui_set_bg_rgb(fd, bg);
        tui_set_fg_rgb(fd, dim);
        tui_set_attr(fd, TUI_ATTR_DIM);
        tui_write(fd, rows[i].info);
        tui_reset_style(fd);
    }

    (void)dim;
    return TUI_OK;
}

static int recent_changes(int fd, const tui_rect_t *area, void *udata)
{
    (void)udata;
    const tui_theme_t *th = tui_theme_current();
    tui_rgb_t card_bg = card_bg_color();
    tui_rgb_t surf_fg = th ? th->surface_fg : (tui_rgb_t){229,229,229};
    tui_rgb_t grad[3] = {
        th ? th->gradient[4] : (tui_rgb_t){74,222,128},
        th ? th->gradient[3] : (tui_rgb_t){34,211,238},
        th ? th->gradient[2] : (tui_rgb_t){249,226,175},
    };
    tui_rgb_t row_alt = row_alt_color();

    const struct {
        char    mark;     /* + / ~ / - */
        const char *text; /* 描述 */
        tui_rgb_t color;
        int     alt;
    } items[] = {
        { '+', "feat: 24-bit color support",         grad[0], 0 },
        { '+', "docs: THEMES.md w/ screenshots",     grad[0], 1 },
        { '~', "refactor: split widget v1/v2",       grad[1], 0 },
        { '+', "feat: Tabs/Stat/Sparkline widgets",  grad[0], 1 },
        { '~', "fix: CJK characters render OK",      grad[1], 0 },
        { '+', "feat: 6 preset themes (Nord etc)",   grad[0], 1 },
    };

    int y = area->row;
    int content_x = area->col;
    int max_x = area->col + area->cols;

    for (int i = 0; i < 6 && y < area->row + area->rows; i++, y++) {
        tui_rgb_t bg = items[i].alt ? row_alt : card_bg;

        /* 行背景 */
        tui_cursor_goto(fd, y, content_x);
        tui_set_bg_rgb(fd, bg);
        tui_spaces(fd, area->cols);
        tui_reset_style(fd);

        /* 标记符 */
        tui_cursor_goto(fd, y, content_x + 1);
        tui_set_bg_rgb(fd, bg);
        tui_set_fg_rgb(fd, items[i].color);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        char m[2] = { items[i].mark, 0 };
        tui_write(fd, m);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, bg);

        /* 文本（按可用宽度截断） */
        tui_cursor_goto(fd, y, content_x + 3);
        tui_set_bg_rgb(fd, bg);
        tui_set_fg_rgb(fd, surf_fg);
        tui_set_attr(fd, TUI_ATTR_RESET);
        int avail = max_x - (content_x + 3) - 1;
        if (avail < 0) avail = 0;
        int bytes = tui_truncate(items[i].text, avail);
        if (bytes > 0) write(fd, items[i].text, (size_t)bytes);
        tui_reset_style(fd);
    }
    return TUI_OK;
}

/* ── 主内容区 ──────────────────────────────────────── */

static tui_tab_t  demo_tabs[6];
static int        demo_ntabs = 6;
static int        demo_selected = 0;

static tui_keyhint_t demo_hints[6];
static int           demo_nhints = 5;

static tui_layout_t *build_root(int theme_idx);

/* 渲染 1 个 stat 卡（在固定 rect 内手动绘制，避免动态分配） */
static void render_stat(int fd, int y, int x, int w, int h,
                        const char *label, const char *value,
                        tui_color_t val_fg, int trend, const char *delta)
{
    const tui_theme_t *th = tui_theme_current();
    tui_rgb_t surf_bg = th ? th->surface_bg : (tui_rgb_t){13,13,23};
    tui_rgb_t surf_fg = th ? th->surface_fg : (tui_rgb_t){229,229,229};
    tui_rgb_t dim     = (tui_rgb_t){110, 115, 130};

    /* 卡片背景 = 比 surface 稍亮一点点（stat 卡用浅背景突出） */
    tui_rgb_t card_bg = {
        (uint8_t)(surf_bg.r + 6),
        (uint8_t)(surf_bg.g + 6),
        (uint8_t)(surf_bg.b + 6)
    };
    tui_rgb_t vc = (val_fg == TUI_COLOR_DEFAULT)
        ? surf_fg : tui_color_to_rgb_xterm(val_fg);

    /* 背景填充 */
    for (int r = 0; r < h; r++) {
        tui_cursor_goto(fd, y + r, x);
        tui_set_bg_rgb(fd, card_bg);
        tui_spaces(fd, w);
    }
    tui_reset_style(fd);

    /* 左侧色条（accent 边） */
    tui_rgb_t bar = th ? th->gradient[4] : (tui_rgb_t){46,160,67};
    for (int r = 0; r < h; r++) {
        tui_cursor_goto(fd, y + r, x);
        tui_set_bg_rgb(fd, bar);
        write(fd, " ", 1);
    }
    tui_reset_style(fd);

    /* 标签（dim） */
    tui_cursor_goto(fd, y, x + 2);
    tui_set_bg_rgb(fd, card_bg);
    tui_set_fg_rgb(fd, dim);
    tui_set_attr(fd, TUI_ATTR_DIM);
    tui_write(fd, label);
    tui_reset_style(fd);
    tui_set_bg_rgb(fd, card_bg);

    /* 数值（大、加粗） */
    if (h >= 2) {
        tui_cursor_goto(fd, y + 1, x + 2);
        tui_set_bg_rgb(fd, card_bg);
        tui_set_fg_rgb(fd, vc);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, value);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, card_bg);
    }

    /* 趋势 */
    if (h >= 3 && (trend != 0 || (delta && delta[0]))) {
        tui_cursor_goto(fd, y + 2, x + 2);
        tui_set_bg_rgb(fd, card_bg);
        const char *arrow = "─";
        tui_rgb_t ac = dim;
        if (trend > 0)      { arrow = "▲"; ac = tui_color_to_rgb_xterm(TUI_COLOR_GREEN); }
        else if (trend < 0) { arrow = "▼"; ac = tui_color_to_rgb_xterm(TUI_COLOR_RED);   }

        tui_set_fg_rgb(fd, ac);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, arrow);
        tui_set_attr(fd, TUI_ATTR_RESET);
        tui_set_bg_rgb(fd, card_bg);  /* RESET 清掉 bg，必须立即恢复 */
        if (delta && delta[0]) {
            tui_set_bg_rgb(fd, card_bg);
            tui_set_fg_rgb(fd, ac);
            tui_set_attr(fd, TUI_ATTR_DIM);
            tui_write(fd, " ");
            tui_write(fd, delta);
        }
        tui_reset_style(fd);
    }
}

static int main_content(int fd, const tui_rect_t *area, void *udata)
{
    (void)udata;
    const tui_theme_t *th = tui_theme_current();
    (void)th;

    /* ── 顶部: 6 个 Stat 卡片横向排列（无 spacer，紧凑） ── */
    int y = area->row;
    int x = area->col;
    int w = area->cols;
    int stat_h = 3;
    int stat_gap = 1;  /* 卡片间隙 */
    int stat_total = 5 * stat_gap;
    int stat_w = (w - stat_total) / 6;
    if (stat_w < 11) stat_w = 11;

    const struct {
        const char *label;
        const char *value;
        tui_color_t fg;
        int trend;
        const char *delta;
    } stats[6] = {
        { "CPU",     "67%",  TUI_COLOR_DEFAULT,  TUI_TREND_UP,   "+3.2%"  },
        { "MEM",     "4.2G", TUI_COLOR_CYAN,     TUI_TREND_UP,   "+120M"  },
        { "DISK",    "84%",  TUI_COLOR_YELLOW,   TUI_TREND_UP,   "+2.1%"  },
        { "NET",     "12M",  TUI_COLOR_GREEN,    TUI_TREND_DOWN, "-1.4M"  },
        { "TASKS",   "128",  TUI_COLOR_DEFAULT,  TUI_TREND_FLAT, "±0"     },
        { "UPTIME",  "14d",  TUI_COLOR_MAGENTA,  TUI_TREND_FLAT, "stable" },
    };

    for (int i = 0; i < 6; i++) {
        int sx = x + i * (stat_w + stat_gap);
        render_stat(fd, y, sx, stat_w, stat_h,
                    stats[i].label, stats[i].value, stats[i].fg,
                    stats[i].trend, stats[i].delta);
    }
    y += stat_h;

    /* ── 中间行：左 Banner + 右 Card(Sparkline) ── */
    int mid_h = 4;
    int mid_w = w / 2 - 1;

    /* 左侧：渐变 Banner */
    {
        tui_banner_t *b = calloc(1, sizeof(tui_banner_t));
        strcpy(b->text, "MeuOS Kit");
        strcpy(b->sub,  "v2 visual upgrade ready");
        b->color = TUI_COLOR_DEFAULT;
        b->style = TUI_BANNER_HEAVY;
        b->gradient = 1;
        b->tag = "v2.0";

        tui_rect_t br = { y, x, mid_h, mid_w };
        tui_banner_render(fd, &br, b);
        free(b);
    }

    /* 右侧：Sparkline + 标签（作为单一卡片） */
    {
        const tui_theme_t *th2 = tui_theme_current();
        tui_rgb_t card_bg = card_bg_color();
        tui_rgb_t accent = th2 ? th2->gradient[4] : (tui_rgb_t){46,160,67};
        tui_rgb_t dim    = (tui_rgb_t){100, 100, 120};
        tui_rgb_t fg     = th2 ? th2->surface_fg : (tui_rgb_t){229,229,229};
        tui_rgb_t spark_fg = th2 ? th2->gradient[3] : (tui_rgb_t){74,222,128};

        int rx = x + mid_w + 1;
        int rw = w - mid_w - 1;
        int rh = mid_h;

        /* 卡片背景 */
        for (int r = 0; r < rh; r++) {
            tui_cursor_goto(fd, y + r, rx);
            tui_set_bg_rgb(fd, card_bg);
            tui_spaces(fd, rw);
        }
        tui_reset_style(fd);

        /* 卡片左色条 */
        for (int r = 0; r < rh; r++) {
            tui_cursor_goto(fd, y + r, rx);
            tui_set_bg_rgb(fd, accent);
            write(fd, " ", 1);
        }
        tui_reset_style(fd);

        /* 标题行 */
        tui_cursor_goto(fd, y, rx + 2);
        tui_set_bg_rgb(fd, card_bg);
        tui_set_fg_rgb(fd, accent);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, "▸ ");
        tui_set_fg_rgb(fd, fg);
        tui_set_attr(fd, TUI_ATTR_RESET);
        tui_write(fd, "CPU 24h");
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, card_bg);

        /* 副标题（实时指示） */
        const char *sub = "live";
        int sub_w = tui_strwidth(sub);
        if (rw - 4 > sub_w + 2) {
            tui_cursor_goto(fd, y, rx + rw - sub_w - 2);
            tui_set_bg_rgb(fd, card_bg);
            tui_set_fg_rgb(fd, dim);
            tui_set_attr(fd, TUI_ATTR_ITALIC | TUI_ATTR_DIM);
            tui_write(fd, sub);
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, card_bg);
        }

        /* 分割线 */
        tui_cursor_goto(fd, y + 1, rx + 1);
        tui_set_bg_rgb(fd, card_bg);
        tui_set_fg_rgb(fd, dim);
        tui_set_attr(fd, TUI_ATTR_DIM);
        for (int i = 0; i < rw - 2; i++) write(fd, "─", 3);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, card_bg);

        /* sparkline 区：y+2..y+rh-1, 占据完整卡片宽度 */
        tui_rect_t sr = { y + 2, rx + 1, rh - 2, rw - 2 };
        /* 居中显示 */
        tui_sparkline_t sp = {
            .data = cpu_data, .npoints = 24, .max_val = 100,
            .fg = TUI_COLOR_DEFAULT, .filled = 0
        };
        /* 直接渲染：sparkline 不需要 surface_bg */
        {
            int maxv = sp.max_val > 0 ? sp.max_val : 100;
            int n = sp.npoints;
            if (n > sr.cols) n = sr.cols;
            int start = (sr.cols - n) / 2;
            if (start < 0) start = 0;

            /* sparkline 背景 */
            tui_cursor_goto(fd, sr.row, sr.col);
            tui_set_bg_rgb(fd, card_bg);
            tui_spaces(fd, sr.cols);
            tui_reset_style(fd);

            tui_cursor_goto(fd, sr.row, sr.col);
            tui_set_bg_rgb(fd, card_bg);
            tui_spaces(fd, start);
            for (int i = 0; i < n; i++) {
                int v = sp.data[i];
                if (v < 0) v = 0;
                if (v > maxv) v = maxv;
                int idx = (v * 8 + maxv - 1) / maxv;
                if (idx > 8) idx = 8;
                if (idx < 1) idx = 1;
                /* 8 段高度字符（U+2581..U+2588） */
                static const char *sch[] = {
                    " ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"
                };
                tui_set_fg_rgb(fd, spark_fg);
                tui_set_attr(fd, TUI_ATTR_BOLD);
                write(fd, sch[idx], 3);
            }
            tui_reset_style(fd);
        }
    }
    y += mid_h;

    /* ── 下行：左 Card (Status) + 右 Card (Changes) ── */
    int bot_h = 8;
    int bot_w = w / 2 - 1;

    /* 左 Card */
    {
        const tui_theme_t *th2 = tui_theme_current();
        tui_rgb_t card_bg = card_bg_color();
        tui_rgb_t accent = th2 ? th2->gradient[4] : (tui_rgb_t){46,160,67};
        tui_rgb_t dim    = (tui_rgb_t){100, 100, 120};
        tui_rgb_t fg     = th2 ? th2->surface_fg : (tui_rgb_t){229,229,229};

        int rx = x;
        int rw = bot_w;

        /* 卡片背景 */
        for (int r = 0; r < bot_h; r++) {
            tui_cursor_goto(fd, y + r, rx);
            tui_set_bg_rgb(fd, card_bg);
            tui_spaces(fd, rw);
        }
        tui_reset_style(fd);

        /* 左色条 */
        for (int r = 0; r < bot_h; r++) {
            tui_cursor_goto(fd, y + r, rx);
            tui_set_bg_rgb(fd, accent);
            write(fd, " ", 1);
        }
        tui_reset_style(fd);

        /* 标题 */
        tui_cursor_goto(fd, y, rx + 2);
        tui_set_bg_rgb(fd, card_bg);
        tui_set_fg_rgb(fd, accent);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, "▸ ");
        tui_set_fg_rgb(fd, fg);
        tui_set_attr(fd, TUI_ATTR_RESET);
        tui_write(fd, "Service Status");
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, card_bg);

        /* 副标题 */
        const char *sub = "6 systems";
        int sub_w = tui_strwidth(sub);
        if (rw - 4 > sub_w + 2) {
            tui_cursor_goto(fd, y, rx + rw - sub_w - 2);
            tui_set_bg_rgb(fd, card_bg);
            tui_set_fg_rgb(fd, dim);
            tui_set_attr(fd, TUI_ATTR_ITALIC | TUI_ATTR_DIM);
            tui_write(fd, sub);
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, card_bg);
        }

        /* 分隔线 */
        tui_cursor_goto(fd, y + 1, rx + 1);
        tui_set_bg_rgb(fd, card_bg);
        tui_set_fg_rgb(fd, dim);
        tui_set_attr(fd, TUI_ATTR_DIM);
        for (int i = 0; i < rw - 2; i++) write(fd, "─", 3);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, card_bg);

        /* 内容区：y+2..y+bot_h-1 */
        tui_rect_t cr = { y + 2, rx + 1, bot_h - 2, rw - 2 };
        status_card_content(fd, &cr, NULL);
    }
    /* 右 Card */
    {
        const tui_theme_t *th2 = tui_theme_current();
        tui_rgb_t card_bg = card_bg_color();
        tui_rgb_t accent = th2 ? th2->gradient[1] : (tui_rgb_t){56,189,248};
        tui_rgb_t dim    = (tui_rgb_t){100, 100, 120};
        tui_rgb_t fg     = th2 ? th2->surface_fg : (tui_rgb_t){229,229,229};

        int rx = x + bot_w + 1;
        int rw = w - bot_w - 1;

        for (int r = 0; r < bot_h; r++) {
            tui_cursor_goto(fd, y + r, rx);
            tui_set_bg_rgb(fd, card_bg);
            tui_spaces(fd, rw);
        }
        tui_reset_style(fd);

        for (int r = 0; r < bot_h; r++) {
            tui_cursor_goto(fd, y + r, rx);
            tui_set_bg_rgb(fd, accent);
            write(fd, " ", 1);
        }
        tui_reset_style(fd);

        tui_cursor_goto(fd, y, rx + 2);
        tui_set_bg_rgb(fd, card_bg);
        tui_set_fg_rgb(fd, accent);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, "▸ ");
        tui_set_fg_rgb(fd, fg);
        tui_set_attr(fd, TUI_ATTR_RESET);
        tui_write(fd, "Recent Changes");
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, card_bg);

        const char *sub = "git log";
        int sub_w = tui_strwidth(sub);
        if (rw - 4 > sub_w + 2) {
            tui_cursor_goto(fd, y, rx + rw - sub_w - 2);
            tui_set_bg_rgb(fd, card_bg);
            tui_set_fg_rgb(fd, dim);
            tui_set_attr(fd, TUI_ATTR_ITALIC | TUI_ATTR_DIM);
            tui_write(fd, sub);
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, card_bg);
        }

        tui_cursor_goto(fd, y + 1, rx + 1);
        tui_set_bg_rgb(fd, card_bg);
        tui_set_fg_rgb(fd, dim);
        tui_set_attr(fd, TUI_ATTR_DIM);
        for (int i = 0; i < rw - 2; i++) write(fd, "─", 3);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, card_bg);

        tui_rect_t cr = { y + 2, rx + 1, bot_h - 2, rw - 2 };
        recent_changes(fd, &cr, NULL);
    }
    y += bot_h;

    /* ── 底部：Processes 表格（斑马纹） ──
     * 高度：填充剩余 content 空间；NAME 列自适应宽度 */
    int proc_h = (area->row + area->rows) - y;
    if (proc_h < 6) proc_h = 6;
    {
        int name_w = w - 6 - 1 - 7 - 1 - 8 - 3;  /* 其余全给 NAME */
        if (name_w < 20) name_w = 20;

        tui_table_t tbl;
        memset(&tbl, 0, sizeof(tbl));
        strcpy(tbl.columns[0].header, "PID");  tbl.columns[0].width = 6;  tbl.columns[0].align = 0;
        strcpy(tbl.columns[1].header, "NAME"); tbl.columns[1].width = name_w; tbl.columns[1].align = -1;
        strcpy(tbl.columns[2].header, "CPU%"); tbl.columns[2].width = 7;  tbl.columns[2].align = 1;
        strcpy(tbl.columns[3].header, "STAT"); tbl.columns[3].width = 8;  tbl.columns[3].align = 0;
        tbl.ncols = 4;
        tbl.nrows = 7;
        tbl.cell_fn = processes_cb;
        tbl.selected = 3;
        tbl.header_bg = TUI_COLOR_DEFAULT;
        tbl.select_bg = TUI_COLOR_DEFAULT;

        /* 表格卡片背景 */
        const tui_theme_t *th2 = tui_theme_current();
        tui_rgb_t card_bg = card_bg_color();
        tui_rgb_t accent = th2 ? th2->gradient[4] : (tui_rgb_t){46,160,67};
        tui_rgb_t dim    = (tui_rgb_t){100, 100, 120};
        tui_rgb_t fg     = th2 ? th2->surface_fg : (tui_rgb_t){229,229,229};

        int tx = x;
        int tw = w;
        int th = proc_h;
        for (int r = 0; r < th; r++) {
            tui_cursor_goto(fd, y + r, tx);
            tui_set_bg_rgb(fd, card_bg);
            tui_spaces(fd, tw);
        }
        tui_reset_style(fd);

        /* 顶行标题 */
        tui_cursor_goto(fd, y, tx);
        tui_set_bg_rgb(fd, card_bg);
        tui_set_fg_rgb(fd, accent);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, "▸ ");
        tui_set_fg_rgb(fd, fg);
        tui_set_attr(fd, TUI_ATTR_RESET);
        tui_write(fd, "Top Processes");
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, card_bg);

        /* 副标题 */
        const char *sub = "by CPU";
        int sub_w = tui_strwidth(sub);
        if (tw - 4 > sub_w + 2) {
            tui_cursor_goto(fd, y, tx + tw - sub_w - 2);
            tui_set_bg_rgb(fd, card_bg);
            tui_set_fg_rgb(fd, dim);
            tui_set_attr(fd, TUI_ATTR_ITALIC | TUI_ATTR_DIM);
            tui_write(fd, sub);
            tui_reset_style(fd);
            tui_set_bg_rgb(fd, card_bg);
        }

        /* 分隔线 */
        tui_cursor_goto(fd, y + 1, tx);
        tui_set_bg_rgb(fd, card_bg);
        tui_set_fg_rgb(fd, dim);
        tui_set_attr(fd, TUI_ATTR_DIM);
        for (int i = 0; i < tw; i++) write(fd, "─", 3);
        tui_reset_style(fd);
        tui_set_bg_rgb(fd, card_bg);

        /* 表格区域：填充卡片剩余高度 */
        int table_rows = proc_h - 2;
        if (table_rows < 4) table_rows = 4;
        tui_rect_t r = { y + 2, tx, table_rows, tw };
        tui_table_render(fd, &r, &tbl);
    }
    y += proc_h;

    return TUI_OK;
}

/* 自定义 header：左侧 logo + 标题（渐变） + 右侧 chip（主题） + 时间戳 */
static int custom_header(int fd, const tui_rect_t *area, void *udata)
{
    (void)udata;
    const tui_theme_t *th = tui_theme_current();
    if (!th) th = &tui_theme_meuos;
    tui_rgb_t hbg = th->header_bg;
    tui_rgb_t hfg = th->header_fg;

    int y = area->row, x = area->col, w = area->cols;
    if (w < 10) return TUI_OK;

    /* 整行底色 */
    tui_cursor_goto(fd, y, x);
    tui_set_bg_rgb(fd, hbg);
    tui_spaces(fd, w);
    tui_reset_style(fd);

    /* 左侧 logo：■ + 间距 */
    tui_cursor_goto(fd, y, x);
    tui_set_bg_rgb(fd, hbg);
    tui_set_fg_rgb(fd, hfg);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_write(fd, " ▸ ");
    tui_reset_style(fd);
    tui_set_bg_rgb(fd, hbg);

    /* 标题 */
    const char *title = "MeuOS Kit Dashboard";
    tui_cursor_goto(fd, y, x + 4);
    tui_set_fg_rgb(fd, hfg);
    tui_set_bg_rgb(fd, hbg);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_write(fd, title);
    tui_reset_style(fd);
    tui_set_bg_rgb(fd, hbg);

    /* 右侧布局：chip + 间隔 + 时间戳
     * chip_w = " Nome " 长度
     * 总宽度 = chip_w + 2 + 8
     * 整体右对齐到 w-1
     */
    int tlen = tui_strwidth(title);
    int chip_w = (int)strlen(th->name) + 2;  /* " MeuOS " 风格 */
    int right_total = chip_w + 2 + 8;        /* chip + 间隔 + "HH:MM:SS" */
    int start = x + w - right_total;
    if (start < x + tlen + 6) start = x + tlen + 6;
    if (start + right_total > x + w) start = x + w - right_total;

    /* 主题 chip（反色） */
    tui_cursor_goto(fd, y, start);
    tui_set_bg_rgb(fd, hfg);
    tui_set_fg_rgb(fd, hbg);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_write(fd, " ");
    tui_write(fd, th->name);
    tui_write(fd, " ");
    tui_reset_style(fd);
    tui_set_bg_rgb(fd, hbg);

    /* 分隔点 */
    tui_cursor_goto(fd, y, start + chip_w + 1);
    tui_set_bg_rgb(fd, hbg);
    tui_set_fg_rgb(fd, hfg);
    tui_set_attr(fd, TUI_ATTR_DIM);
    tui_write(fd, "│");
    tui_reset_style(fd);
    tui_set_bg_rgb(fd, hbg);

    /* 时间戳（HH:MM） */
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char ts[16];
    strftime(ts, sizeof(ts), "%H:%M", &tmv);
    tui_cursor_goto(fd, y, start + chip_w + 3);
    tui_set_bg_rgb(fd, hbg);
    tui_set_fg_rgb(fd, hfg);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_write(fd, ts);
    tui_reset_style(fd);
    tui_set_bg_rgb(fd, hbg);

    return TUI_OK;
}

static tui_layout_t *build_root(int theme_idx)
{
    tui_set_theme(tui_themes[theme_idx]);

    /* Tab 数据 */
    demo_tabs[0] = (tui_tab_t){ "Overview", "live",  0, 0 };
    demo_tabs[1] = (tui_tab_t){ "System",   "6",     0, 0 };
    demo_tabs[2] = (tui_tab_t){ "Network",  NULL,    0, 0 };
    demo_tabs[3] = (tui_tab_t){ "Logs",     "12k",   0, 0 };
    demo_tabs[4] = (tui_tab_t){ "Users",    NULL,    0, 0 };
    demo_tabs[5] = (tui_tab_t){ "Settings", "new",   0, 0 };
    demo_tabs[demo_selected].active = 1;

    /* KeyHints */
    demo_hints[0] = (tui_keyhint_t){ "1-6", "themes", TUI_COLOR_DEFAULT };
    demo_hints[1] = (tui_keyhint_t){ "Tab", "switch", TUI_COLOR_CYAN    };
    demo_hints[2] = (tui_keyhint_t){ "R",   "refresh",TUI_COLOR_YELLOW  };
    demo_hints[3] = (tui_keyhint_t){ "?",   "help",   TUI_COLOR_GREEN   };
    demo_hints[4] = (tui_keyhint_t){ "Q",   "quit",   TUI_COLOR_RED     };

    /* vbox: header + tabs + content + keyhints */
    tui_layout_t *root = tui_layout_vbox(0);
    if (!root) return NULL;

    tui_layout_t *hdr = tui_layout_leaf(custom_header, NULL);
    tui_layout_add(root, hdr, 0);

    tui_layout_t *tabs = tui_tabbar_new(demo_tabs, demo_ntabs, demo_selected);
    tui_layout_add(root, tabs, 0);

    tui_layout_t *content = tui_layout_leaf(main_content, NULL);
    tui_layout_add(root, content, 1);

    tui_layout_t *kh = tui_keyhints_new(demo_hints, demo_nhints);
    tui_layout_add_flex(root, kh, 0, 2);  /* basis=2 = 固定 2 行 */

    return root;
}

int main(int argc, char **argv)
{
    int theme_idx = 0;

    /* 优先从环境变量读取主题名（capture 脚本用） */
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
    if (capture) { sz.rows = 30; sz.cols = 80; }
    else if (tui_get_size(ofd, &sz) != TUI_OK) { sz.rows = 30; sz.cols = 80; }

    tui_layout_t *root = build_root(theme_idx);
    if (root) {
        tui_rect_t area = { 1, 1, sz.rows, sz.cols - 1 };
        /* 用 surface_bg 铺满整个画布，避免空隙区域显示终端默认黑色
         * （否则各 Card 之间会露出 (13,13,26) 暗带，所有主题都受影响） */
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

    /* 交互循环：1-6 切主题，q 退出 */
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
