/* input.c — 输入解析
 *
 * 将终端输入流（ASCII 字符 + 转义序列）统一解析为 tui_event_t。
 * 支持方向键、功能键、鼠标事件（SGR 模式）和超时输入。
 * 纯 C11 + POSIX 实现。
 */

#define _XOPEN_SOURCE 700

#include "meuos/libtui.h"

#include <unistd.h>
#include <poll.h>
#include <string.h>
#include <errno.h>

/* ── 简单 atoi（不含 strtol 依赖展开）─────────────── */

static int atoi_se(const char *s)
{
    int n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return n;
}

/* ── 内部：读一个字节 ─────────────────────────────── */

static int read_byte(int fd, char *c)
{
    ssize_t n = read(fd, c, 1);
    if (n == 1) return TUI_OK;
    if (n == 0) return TUI_KEY_TIMEOUT;
    return TUI_ERR_IO;
}

/* ── 内部：解析 CSI 序列 ───────────────────────────── */

static int parse_csi(int fd, tui_event_t *ev)
{
    char buf[16];
    int  i = 0;
    int  nread = 0;

    /* 收集 CSI 参数 (最多 16 字节) */
    while (i < (int)sizeof(buf) - 1) {
        char c;
        if (read_byte(fd, &c) != TUI_OK) {
            /* 序列不完整，回退为 ESC */
            ev->key = TUI_KEY_ESC;
            return TUI_OK;
        }
        nread++;
        buf[i++] = c;

        /* 终止字符: 字母 (0x40-0x7E) 或 ~ */
        if (c >= 0x40 && c <= 0x7E) break;
        if (c == '~') break;
    }
    buf[i] = '\0';

    /* 解析参数 */
    int params[4];
    int np = 0;
    char *token;
    char *rest = buf;
    int has_param = (buf[0] >= '0' && buf[0] <= '9');

    if (has_param) {
        while ((token = strtok_r(rest, ";", &rest)) && np < 4) {
            params[np++] = atoi_se(token);
        }
    }

    char term = buf[i - 1];  /* 终止字符 */

    /* 方向键 - 简写 */
    if (np == 0 || (np == 1 && params[0] == 1)) {
        switch (term) {
            case 'A': ev->key = TUI_KEY_UP;     return TUI_OK;
            case 'B': ev->key = TUI_KEY_DOWN;   return TUI_OK;
            case 'C': ev->key = TUI_KEY_RIGHT;  return TUI_OK;
            case 'D': ev->key = TUI_KEY_LEFT;   return TUI_OK;
            case 'H': ev->key = TUI_KEY_HOME;   return TUI_OK;
            case 'F': ev->key = TUI_KEY_END;    return TUI_OK;
            default:  break;
        }
    }

    /* 带参数的方向键 */
    if (np >= 1 && term >= 'A' && term <= 'D') {
        switch (term) {
            case 'A': ev->key = TUI_KEY_UP;    return TUI_OK;
            case 'B': ev->key = TUI_KEY_DOWN;  return TUI_OK;
            case 'C': ev->key = TUI_KEY_RIGHT; return TUI_OK;
            case 'D': ev->key = TUI_KEY_LEFT;  return TUI_OK;
        }
    }

    /* Home/End 带参数 */
    if (np >= 1 && (term == 'H' || term == 'F')) {
        ev->key = (term == 'H') ? TUI_KEY_HOME : TUI_KEY_END;
        return TUI_OK;
    }

    /* ~ 终止的序列 (如 \033[3~ = DEL) */
    if (term == '~') {
        switch (np >= 1 ? params[0] : 0) {
            case 1: ev->key = TUI_KEY_HOME; return TUI_OK;
            case 2: ev->key = TUI_KEY_INS;  return TUI_OK;
            case 3: ev->key = TUI_KEY_DEL;  return TUI_OK;
            case 4: ev->key = TUI_KEY_END;  return TUI_OK;
            case 5: ev->key = TUI_KEY_PGUP; return TUI_OK;
            case 6: ev->key = TUI_KEY_PGDN; return TUI_OK;
            case 11: ev->key = TUI_KEY_F1;  return TUI_OK;
            case 12: ev->key = TUI_KEY_F2;  return TUI_OK;
            case 13: ev->key = TUI_KEY_F3;  return TUI_OK;
            case 14: ev->key = TUI_KEY_F4;  return TUI_OK;
            case 15: ev->key = TUI_KEY_F5;  return TUI_OK;
            case 17: ev->key = TUI_KEY_F6;  return TUI_OK;
            case 18: ev->key = TUI_KEY_F7;  return TUI_OK;
            case 19: ev->key = TUI_KEY_F8;  return TUI_OK;
            case 20: ev->key = TUI_KEY_F9;  return TUI_OK;
            case 21: ev->key = TUI_KEY_F10; return TUI_OK;
            case 23: ev->key = TUI_KEY_F11; return TUI_OK;
            case 24: ev->key = TUI_KEY_F12; return TUI_OK;
            default: break;
        }
    }

    /* SGR 鼠标事件: \033[<Cb;Cx;CyM 或 m */
    if (np >= 3 && buf[0] == '<') {
        ev->key = TUI_KEY_MOUSE;
        ev->mouse.button   = params[0] & 0x03;
        ev->mouse.pressed  = (term == 'M');
        ev->mouse.x        = params[1];
        ev->mouse.y        = params[2];
        return TUI_OK;
    }

    /* 未识别的 CSI 序列 — 返回 ESC */
    ev->key = TUI_KEY_ESC;
    return TUI_OK;
}

/* ── tui_getkey — 阻塞读取按键 ────────────────────── */

int tui_getkey(int fd, tui_event_t *ev)
{
    return tui_getkey_timeout(fd, ev, -1);
}

/* ── tui_getkey_timeout — 带超时读取 ───────────────── */

int tui_getkey_timeout(int fd, tui_event_t *ev, int timeout_ms)
{
    char c;

    if (!ev || fd < 0) return TUI_ERR_PARAM;

    memset(ev, 0, sizeof(*ev));

    /* 等待输入 (poll) */
    if (timeout_ms >= 0) {
        struct pollfd pfd;
        pfd.fd     = fd;
        pfd.events = POLLIN;

        int ret = poll(&pfd, 1, timeout_ms);
        if (ret < 0) {
            if (errno == EINTR) {
                ev->key = TUI_KEY_RESIZE;
                return TUI_OK;
            }
            return TUI_ERR_IO;
        }
        if (ret == 0) {
            ev->key = TUI_KEY_TIMEOUT;
            return TUI_OK;
        }
    }

    /* 读取第一个字节 */
    int r = read_byte(fd, &c);
    if (r != TUI_OK) {
        ev->key = r == TUI_KEY_TIMEOUT ? TUI_KEY_TIMEOUT : TUI_KEY_ERR;
        return TUI_OK;
    }

    /* 解析 */
    if (c == '\033') {
        /* 可能是 ESC 或转义序列开头 */
        struct pollfd pfd;
        pfd.fd     = fd;
        pfd.events = POLLIN;

        int ret = poll(&pfd, 1, 20);  /* 20ms 等待后续字节 */
        if (ret <= 0) {
            /* 超时或错误 — 就是 ESC 键 */
            ev->key = TUI_KEY_ESC;
            return TUI_OK;
        }

        char c2;
        if (read_byte(fd, &c2) != TUI_OK) {
            ev->key = TUI_KEY_ESC;
            return TUI_OK;
        }

        if (c2 == '[') {
            return parse_csi(fd, ev);
        } else if (c2 == 'O') {
            /* SS3 序列: \033O... (如 F1-F4 老式编码) */
            char c3;
            if (read_byte(fd, &c3) != TUI_OK) {
                ev->key = TUI_KEY_ESC;
                return TUI_OK;
            }
            switch (c3) {
                case 'P': ev->key = TUI_KEY_F1;  return TUI_OK;
                case 'Q': ev->key = TUI_KEY_F2;  return TUI_OK;
                case 'R': ev->key = TUI_KEY_F3;  return TUI_OK;
                case 'S': ev->key = TUI_KEY_F4;  return TUI_OK;
                case 'H': ev->key = TUI_KEY_HOME; return TUI_OK;
                case 'F': ev->key = TUI_KEY_END;  return TUI_OK;
                default:
                    ev->key = TUI_KEY_ESC;
                    return TUI_OK;
            }
        } else {
            /* 未知序列，当作 ESC */
            ev->key = TUI_KEY_ESC;
            return TUI_OK;
        }
    } else if (c == 0x7F) {
        ev->key = TUI_KEY_BS;
    } else if (c == 0x09) {
        ev->key = TUI_KEY_TAB;
    } else {
        ev->key = (tui_key_t)(unsigned char)c;
    }

    return TUI_OK;
}

/* ── 输出函数 ─────────────────────────────────────── */

int tui_putchar(int fd, char c)
{
    if (write(fd, &c, 1) != 1) return TUI_ERR_IO;
    return TUI_OK;
}

int tui_write(int fd, const char *s)
{
    size_t len = strlen(s);
    if (write(fd, s, len) != (ssize_t)len) return TUI_ERR_IO;
    return TUI_OK;
}

int tui_flush(int fd)
{
    /* Linux 终端 write 是直接输出的，flush 即 fsync */
    (void)fd;
    return TUI_OK;
}
