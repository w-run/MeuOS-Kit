/* terminal.c — 终端 I/O
 *
 * 原始模式设置、备用屏幕切换、鼠标支持和 SIGWINCH 处理。
 * 纯 C11 + POSIX 实现，零外部依赖。
 */

#define _XOPEN_SOURCE 700

#include "meuos/libtui.h"

#include <termios.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

/* ── 内部状态 ─────────────────────────────────────── */

static struct termios tui_saved_termios;
static int            tui_termios_saved = 0;
static int            tui_raw_active_fd = -1;

static tui_resize_cb  tui_resize_handler = NULL;
static void          *tui_resize_userdata = NULL;

static volatile int   tui_resize_pending = 0;

/* ── SIGWINCH 处理 ────────────────────────────────── */

static void tui_sigwinch_handler(int sig)
{
    (void)sig;
    tui_resize_pending = 1;
}

/* ── 原始模式 ─────────────────────────────────────── */

int tui_raw_mode(int fd, int enable)
{
    if (fd < 0) return TUI_ERR_PARAM;

    if (enable) {
        struct termios raw;

        /* 先保存当前 termios */
        if (tcgetattr(fd, &raw) < 0) return TUI_ERR_IO;

        if (!tui_termios_saved) {
            memcpy(&tui_saved_termios, &raw, sizeof(raw));
            tui_termios_saved = 1;
        }

        /* 手动设置原始模式 (替代 cfmakeraw，避免 _DEFAULT_SOURCE) */
        raw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
                        | INLCR | IGNCR | ICRNL | IXON);
        raw.c_oflag &= ~OPOST;
        raw.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
        raw.c_cflag &= ~(CSIZE | PARENB);
        raw.c_cflag |= CS8;
        raw.c_cc[VMIN]  = 1;
        raw.c_cc[VTIME] = 0;

        if (tcsetattr(fd, TCSAFLUSH, &raw) < 0) return TUI_ERR_IO;
        tui_raw_active_fd = fd;
    } else {
        /* 恢复保存的设置 */
        if (!tui_termios_saved) return TUI_OK;
        if (tcsetattr(fd, TCSAFLUSH, &tui_saved_termios) < 0) return TUI_ERR_IO;
        tui_raw_active_fd = -1;
    }

    return TUI_OK;
}

/* ── 备用屏幕 ─────────────────────────────────────── */

int tui_alt_screen(int fd, int enable)
{
    const char *seq = enable ? "\033[?1049h" : "\033[?1049l";
    size_t len = enable ? 7 : 7;
    if (write(fd, seq, len) != (ssize_t)len) return TUI_ERR_IO;
    return TUI_OK;
}

/* ── 鼠标支持 ─────────────────────────────────────── */

int tui_mouse(int fd, int enable)
{
    /* 启用 SGR 鼠标模式 + 按钮事件 */
    const char *seq_en  = "\033[?1000h\033[?1002h\033[?1006h";
    const char *seq_dis = "\033[?1006l\033[?1002l\033[?1000l";
    size_t len_en  = strlen(seq_en);
    size_t len_dis = strlen(seq_dis);

    if (enable) {
        if (write(fd, seq_en, len_en) != (ssize_t)len_en) return TUI_ERR_IO;
    } else {
        if (write(fd, seq_dis, len_dis) != (ssize_t)len_dis) return TUI_ERR_IO;
    }
    return TUI_OK;
}

/* ── SIGWINCH 回调 ────────────────────────────────── */

int tui_on_resize(tui_resize_cb cb, void *userdata)
{
    struct sigaction sa;

    tui_resize_handler  = cb;
    tui_resize_userdata = userdata;

    if (cb) {
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = tui_sigwinch_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        if (sigaction(SIGWINCH, &sa, NULL) < 0) return TUI_ERR_IO;
    } else {
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        if (sigaction(SIGWINCH, &sa, NULL) < 0) return TUI_ERR_IO;
    }

    return TUI_OK;
}

/* ── 内部：检查并触发 resize 事件 ──────────────────── */

int tui_check_resize(int fd)
{
    if (tui_resize_pending && tui_resize_handler) {
        tui_resize_pending = 0;
        tui_size_t size;
        if (tui_get_size(fd, &size) == TUI_OK) {
            tui_resize_handler(size, tui_resize_userdata);
        }
        return 1;
    }
    return 0;
}
