/* screen.c — 屏幕操作
 *
 * 光标定位、颜色/样式、屏幕清除、终端尺寸查询。
 * 纯 C11 + POSIX 实现，使用 ANSI 转义序列。
 */

#define _XOPEN_SOURCE 700

#include "meuos/libtui.h"

#include <unistd.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <stdio.h>

/* ── 终端尺寸 ─────────────────────────────────────── */

int tui_get_size(int fd, tui_size_t *size)
{
    struct winsize ws;

    if (!size) return TUI_ERR_PARAM;

    if (ioctl(fd, TIOCGWINSZ, &ws) < 0) return TUI_ERR_IO;

    size->rows   = (int)ws.ws_row;
    size->cols   = (int)ws.ws_col;
    size->xpixel = (int)ws.ws_xpixel;
    size->ypixel = (int)ws.ws_ypixel;

    return TUI_OK;
}

/* ── 光标定位 ─────────────────────────────────────── */

int tui_cursor_goto(int fd, int row, int col)
{
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "\033[%d;%dH", row, col);
    if (n <= 0 || n >= (int)sizeof(buf)) return TUI_ERR_PARAM;
    if (write(fd, buf, (size_t)n) != n) return TUI_ERR_IO;
    return TUI_OK;
}

static int tui_cursor_move(int fd, char dir, int n)
{
    char buf[16];
    int sn;

    if (n <= 0) return TUI_OK;
    if (n == 1) {
        buf[0] = '\033';
        buf[1] = '[';
        buf[2] = dir;
        sn = 3;
    } else {
        sn = snprintf(buf, sizeof(buf), "\033[%d%c", n, dir);
        if (sn <= 0 || sn >= (int)sizeof(buf)) return TUI_ERR_PARAM;
    }

    if (write(fd, buf, (size_t)sn) != sn) return TUI_ERR_IO;
    return TUI_OK;
}

int tui_cursor_up(int fd, int n)    { return tui_cursor_move(fd, 'A', n); }
int tui_cursor_down(int fd, int n)  { return tui_cursor_move(fd, 'B', n); }
int tui_cursor_left(int fd, int n)  { return tui_cursor_move(fd, 'D', n); }
int tui_cursor_right(int fd, int n) { return tui_cursor_move(fd, 'C', n); }

/* ── 光标保存/恢复 ────────────────────────────────── */

int tui_cursor_save(int fd)
{
    const char *seq = "\033[s";
    if (write(fd, seq, 3) != 3) return TUI_ERR_IO;
    return TUI_OK;
}

int tui_cursor_restore(int fd)
{
    const char *seq = "\033[u";
    if (write(fd, seq, 3) != 3) return TUI_ERR_IO;
    return TUI_OK;
}

/* ── 光标显隐 ─────────────────────────────────────── */

int tui_cursor_show(int fd, int show)
{
    const char *seq = show ? "\033[?25h" : "\033[?25l";
    size_t len = show ? 6 : 6;
    if (write(fd, seq, len) != (ssize_t)len) return TUI_ERR_IO;
    return TUI_OK;
}

/* ── 清除屏幕 ─────────────────────────────────────── */

int tui_clear_screen(int fd)
{
    const char *seq = "\033[2J\033[H";
    if (write(fd, seq, 7) != 7) return TUI_ERR_IO;
    return TUI_OK;
}

int tui_clear_line(int fd)
{
    const char *seq = "\033[2K\r";
    if (write(fd, seq, 4) != 4) return TUI_ERR_IO;
    return TUI_OK;
}

int tui_clear_eol(int fd)
{
    const char *seq = "\033[K";
    if (write(fd, seq, 3) != 3) return TUI_ERR_IO;
    return TUI_OK;
}

/* ── 颜色和样式 ───────────────────────────────────── */

int tui_set_fg(int fd, tui_color_t c)
{
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "\033[%dm", 30 + (int)c);
    if (n <= 0 || n >= (int)sizeof(buf)) return TUI_ERR_PARAM;
    if (write(fd, buf, (size_t)n) != n) return TUI_ERR_IO;
    return TUI_OK;
}

/* 跟踪最近一次 24-bit bg 设置。
 * 用于 TUI_ATTR_RESET / tui_reset_style() 之后自动恢复 bg，
 * 否则空格/文本会落到终端默认背景 (在 ansi2png 转换器里 = (13,13,26) 黑块)。
 * 默认 (0,0,0) 表示尚未设置过任何 24-bit bg，第一次 reset 时不恢复。 */
static tui_rgb_t g_last_bg = { 0, 0, 0 };
static int g_bg_set = 0;

int tui_set_bg(int fd, tui_color_t c)
{
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "\033[%dm", 40 + (int)c);
    if (n <= 0 || n >= (int)sizeof(buf)) return TUI_ERR_PARAM;
    if (write(fd, buf, (size_t)n) != n) return TUI_ERR_IO;
    /* 16 色 bg 不进入 24-bit 追踪，避免误恢复成不同色 */
    g_bg_set = 0;
    return TUI_OK;
}

int tui_set_attr(int fd, tui_attr_t a)
{
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "\033[%dm", (int)a);
    if (n <= 0 || n >= (int)sizeof(buf)) return TUI_ERR_PARAM;
    if (write(fd, buf, (size_t)n) != n) return TUI_ERR_IO;

    /* TUI_ATTR_RESET (0) 会清掉 bg；自动恢复最近一次设置的 bg，
     * 避免后续写入落到默认 (13,13,26) 黑块。 */
    if (a == TUI_ATTR_RESET && g_bg_set) {
        char bgbuf[32];
        int bn = snprintf(bgbuf, sizeof(bgbuf), "\033[48;2;%d;%d;%dm",
                          (int)g_last_bg.r, (int)g_last_bg.g, (int)g_last_bg.b);
        if (bn > 0 && bn < (int)sizeof(bgbuf)) {
            if (write(fd, bgbuf, (size_t)bn) != bn) return TUI_ERR_IO;
        }
    }
    return TUI_OK;
}

int tui_reset_style(int fd)
{
    const char *seq = "\033[0m";
    if (write(fd, seq, 4) != 4) return TUI_ERR_IO;
    /* 同上：恢复最近一次设置的 bg */
    if (g_bg_set) {
        char bgbuf[32];
        int bn = snprintf(bgbuf, sizeof(bgbuf), "\033[48;2;%d;%d;%dm",
                          (int)g_last_bg.r, (int)g_last_bg.g, (int)g_last_bg.b);
        if (bn > 0 && bn < (int)sizeof(bgbuf)) {
            if (write(fd, bgbuf, (size_t)bn) != bn) return TUI_ERR_IO;
        }
    }
    return TUI_OK;
}

/* ── 24-bit 真彩色 (RGB 直接指定) ───────────────────── */

int tui_set_fg_rgb(int fd, tui_rgb_t c)
{
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "\033[38;2;%d;%d;%dm",
                     (int)c.r, (int)c.g, (int)c.b);
    if (n <= 0 || n >= (int)sizeof(buf)) return TUI_ERR_PARAM;
    if (write(fd, buf, (size_t)n) != n) return TUI_ERR_IO;
    return TUI_OK;
}

int tui_set_bg_rgb(int fd, tui_rgb_t c)
{
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "\033[48;2;%d;%d;%dm",
                     (int)c.r, (int)c.g, (int)c.b);
    if (n <= 0 || n >= (int)sizeof(buf)) return TUI_ERR_PARAM;
    if (write(fd, buf, (size_t)n) != n) return TUI_ERR_IO;
    /* 记录最近一次 24-bit bg，供后续 reset 自动恢复 */
    g_last_bg = c;
    g_bg_set = 1;
    return TUI_OK;
}

/* ── 16 色 → RGB 近似值 ────────────────────────────────
 * 用于截图生成和 16 色模式下的语义颜色。
 * xterm-256color 调色板为更准确的近似。
 */

static const tui_rgb_t ansi_16_table[16] = {
    {  0,   0,   0},   /* 0 BLACK        */
    {170,   0,   0},   /* 1 RED          */
    {  0, 170,   0},   /* 2 GREEN        */
    {170,  85,   0},   /* 3 YELLOW       */
    {  0,   0, 170},   /* 4 BLUE         */
    {170,   0, 170},   /* 5 MAGENTA      */
    {  0, 170, 170},   /* 6 CYAN         */
    {170, 170, 170},   /* 7 WHITE        */
    { 85,  85,  85},   /* 8 BRIGHT BLACK */
    {255,  85,  85},   /* 9 BRIGHT RED   */
    { 85, 255,  85},   /* 10 BRIGHT GREEN */
    {255, 255,  85},   /* 11 BRIGHT YELLOW */
    { 85,  85, 255},   /* 12 BRIGHT BLUE */
    {255,  85, 255},   /* 13 BRIGHT MAGENTA */
    { 85, 255, 255},   /* 14 BRIGHT CYAN */
    {255, 255, 255},   /* 15 BRIGHT WHITE */
};

static const tui_rgb_t xterm_16_table[16] = {
    {  0,   0,   0},   /* 0 BLACK        */
    {205,   0,   0},   /* 1 RED          */
    {  0, 205,   0},   /* 2 GREEN        */
    {205, 205,   0},   /* 3 YELLOW       */
    {  0,   0, 238},   /* 4 BLUE         */
    {205,   0, 205},   /* 5 MAGENTA      */
    {  0, 205, 205},   /* 6 CYAN         */
    {229, 229, 229},   /* 7 WHITE        */
    {127, 127, 127},   /* 8 BRIGHT BLACK */
    {255,   0,   0},   /* 9 BRIGHT RED   */
    {  0, 255,   0},   /* 10 BRIGHT GREEN */
    {255, 255,   0},   /* 11 BRIGHT YELLOW */
    { 92,  92, 255},   /* 12 BRIGHT BLUE */
    {255,   0, 255},   /* 13 BRIGHT MAGENTA */
    {  0, 255, 255},   /* 14 BRIGHT CYAN */
    {255, 255, 255},   /* 15 BRIGHT WHITE */
};

tui_rgb_t tui_color_to_rgb(tui_color_t c)
{
    int idx = (int)c;
    if (idx < 0 || idx > 15) return (tui_rgb_t){229, 229, 229};
    return ansi_16_table[idx];
}

tui_rgb_t tui_color_to_rgb_xterm(tui_color_t c)
{
    int idx = (int)c;
    if (idx < 0 || idx > 15) return (tui_rgb_t){229, 229, 229};
    return xterm_16_table[idx];
}

/* ── 格式化写入 ────────────────────────────────────── */

int tui_printf(int fd, const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0) return TUI_ERR_PARAM;
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;

    if (write(fd, buf, (size_t)n) != n) return TUI_ERR_IO;
    return TUI_OK;
}
