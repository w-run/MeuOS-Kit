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

int tui_set_bg(int fd, tui_color_t c)
{
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "\033[%dm", 40 + (int)c);
    if (n <= 0 || n >= (int)sizeof(buf)) return TUI_ERR_PARAM;
    if (write(fd, buf, (size_t)n) != n) return TUI_ERR_IO;
    return TUI_OK;
}

int tui_set_attr(int fd, tui_attr_t a)
{
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "\033[%dm", (int)a);
    if (n <= 0 || n >= (int)sizeof(buf)) return TUI_ERR_PARAM;
    if (write(fd, buf, (size_t)n) != n) return TUI_ERR_IO;
    return TUI_OK;
}

int tui_reset_style(int fd)
{
    const char *seq = "\033[0m";
    if (write(fd, seq, 4) != 4) return TUI_ERR_IO;
    return TUI_OK;
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
