/* msh/exec/lineedit.c — 行编辑（裸 termios，不依赖 readline）
 *
 * 功能：
 *   - 字符级编辑：左右移动、Home/End、Backspace/Delete
 *   - 上下箭头翻历史
 *   - Ctrl-A/E（行首/行尾）、Ctrl-C 中断、Ctrl-D 空行 EOF
 *   - UTF-8 多字节字符支持（中文不乱码、回退不残缺）
 *   - 终端状态安全恢复（atexit + 信号处理）
 *
 * 输出到 stdout（提示符由调用者绘制或内部绘制）。
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "msh/history.h"
#include "msh/complete.h"

/* ── 终端状态管理 ────────────────────────────────────────────── */

static struct termios g_saved_termios;
static int            g_termios_saved = 0;
static int            g_in_raw_mode   = 0;

/* 恢复终端到原始模式。可安全重复调用。 */
static void term_restore(void) {
    if (g_in_raw_mode && g_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved_termios);
        g_in_raw_mode = 0;
    }
}

/* 进入 raw 模式。返回 0 成功，-1 失败（非 tty 等）。 */
static int term_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &g_saved_termios) < 0)
        return -1;
    g_termios_saved = 1;

    struct termios raw = g_saved_termios;
    /* 禁用 ICANON(行缓冲) / ECHO(回显) / ISIG(信号生成)
     * ISIG 必须禁用：否则 Ctrl-C 会同时被读取为字节 0x03 和触发 SIGINT 信号，
     * 导致 shell 进程被杀死。我们自行处理 Ctrl-C/Ctrl-Z 等按键。 */
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0)
        return -1;

    g_in_raw_mode = 1;

    /* 注册 atexit 恢复（只注册一次） */
    static int atexit_registered = 0;
    if (!atexit_registered) {
        atexit(term_restore);
        atexit_registered = 1;
    }
    return 0;
}

/* 信号处理：恢复终端后重新raise信号 */
static void term_signal_handler(int sig) {
    term_restore();
    /* 重置为默认处理后重新发送 */
    signal(sig, SIG_DFL);
    raise(sig);
}

/* 注册信号处理（在进入 raw 模式后调用） */
static void term_install_signal_handlers(void) {
    static const int sigs[] = { SIGTERM, SIGHUP, SIGQUIT, SIGSEGV, SIGABRT };
    for (size_t i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = term_signal_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(sigs[i], &sa, NULL);
    }
}

/* ── UTF-8 辅助 ─────────────────────────────────────────────── */

/* 返回 UTF-8 前导字节对应的字符长度（字节数）。
 * 1: ASCII (0xxxxxxx)
 * 2: 110xxxxx
 * 3: 1110xxxx
 * 4: 11110xxx
 * 0: 非法/续字节 */
static int utf8_char_len(unsigned char c) {
    if (c < 0x80)      return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;  /* 退化：当作单字节 */
}

/* 计算 buf[0..len) 的显示宽度（字节数 != 显示宽度，但 ASCII 下相等）。
 * 对于 UTF-8 多字节字符，显示宽度 = 字符数（简化：每个多字节字符算 1 列）。
 * 返回 "列数" — 用于光标移动距离。 */
static size_t utf8_display_width(const char *buf, size_t len) {
    size_t width = 0;
    size_t i = 0;
    while (i < len) {
        int cl = utf8_char_len((unsigned char)buf[i]);
        if (cl > (int)(len - i)) cl = 1;  /* 截断保护 */
        width++;
        i += cl;
    }
    return width;
}

/* 从 buf 的字节偏移 byte_pos 向左移动一个字符，返回新的字节偏移。 */
static size_t utf8_prev_char(const char *buf, size_t byte_pos) {
    if (byte_pos == 0) return 0;
    size_t p = byte_pos - 1;
    /* 跳过续字节 (10xxxxxx) */
    while (p > 0 && (buf[p] & 0xC0) == 0x80)
        p--;
    return p;
}

/* 从 buf 的字节偏移 byte_pos 向右移动一个字符，返回新的字节偏移。 */
static size_t utf8_next_char(const char *buf, size_t byte_pos, size_t len) {
    if (byte_pos >= len) return len;
    int cl = utf8_char_len((unsigned char)buf[byte_pos]);
    if (cl > (int)(len - byte_pos)) cl = 1;
    return byte_pos + cl;
}

/* ── 原始写入辅助 ────────────────────────────────────────────── */

/* 直接 write 到 stdout，避免 printf/fputs 混用导致的缓冲问题 */
static void emit(const char *s, size_t n) {
    ssize_t off = 0;
    while ((size_t)off < n) {
        ssize_t w = write(STDOUT_FILENO, s + off, n - off);
        if (w <= 0) break;
        off += w;
    }
}

static void emit_str(const char *s) {
    emit(s, strlen(s));
}

/* 发送 ANSI 光标左移 n 列 */
static void cursor_left(size_t n) {
    if (n == 0) return;
    char buf[32];
    int m = snprintf(buf, sizeof(buf), "\033[%zuD", n);
    if (m > 0) emit(buf, m);
}

/* 发送 ANSI 光标右移 n 列 */
static void cursor_right(size_t n) {
    if (n == 0) return;
    char buf[32];
    int m = snprintf(buf, sizeof(buf), "\033[%zuC", n);
    if (m > 0) emit(buf, m);
}

/* ── 行编辑主函数 ────────────────────────────────────────────── */

/* 读取一个编辑好的行。prompt 非 NULL 时先输出 prompt。
 * 返回 malloc 字符串（不含换行），EOF 返回 NULL。 */
char *msh_readline(const char *prompt) {
    /* 非 tty：退化为 getline */
    if (!isatty(STDIN_FILENO)) {
        if (prompt) { emit_str(prompt); }
        char *line = NULL;
        size_t cap = 0;
        ssize_t n = getline(&line, &cap, stdin);
        if (n < 0) { free(line); return NULL; }
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
        return line;
    }

    /* 进入 raw 模式 */
    if (term_raw_mode() < 0) {
        /* 退化：getline */
        if (prompt) { emit_str(prompt); }
        char *line = NULL;
        size_t cap = 0;
        ssize_t n = getline(&line, &cap, stdin);
        if (n < 0) { free(line); return NULL; }
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
        return line;
    }

    term_install_signal_handlers();

    char *buf = malloc(256);
    size_t cap = 256;
    size_t len = 0;      /* 当前行字节长度 */
    size_t cur = 0;      /* 光标字节偏移 */
    int hist_index = -1; /* -1 表示不在历史中 */

    /* 保存原始缓冲区（用于历史导航后恢复） */
    char *saved_buf = NULL;
    size_t saved_len = 0;

    if (prompt) emit_str(prompt);

    for (;;) {
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1) break;

        /* ── Ctrl-C ── */
        if (c == 3) {
            emit_str("^C\n");
            free(buf);
            free(saved_buf);
            term_restore();
            return strdup("\003");
        }

        /* ── Ctrl-D ── */
        if (c == 4) {
            if (len == 0) {
                /* 空行 EOF */
                free(buf);
                free(saved_buf);
                term_restore();
                return NULL;
            }
            /* 删除光标处字符 */
            if (cur < len) {
                size_t cl = utf8_next_char(buf, cur, len) - cur;
                memmove(buf + cur, buf + cur + cl, len - cur - cl + 1);
                len -= cl;
                /* 重绘 */
                emit_str("\033[K");
                emit(buf + cur, len - cur);
                size_t chars_after = utf8_display_width(buf + cur, len - cur);
                cursor_left(chars_after);
            }
            continue;
        }

        /* ── Enter ── */
        if (c == 10 || c == 13) {
            emit_str("\n");
            break;
        }

        /* ── ESC 序列（箭头/Home/End/Delete 等） ── */
        if (c == 27) {
            char s1, s2;
            if (read(STDIN_FILENO, &s1, 1) != 1) break;
            if (s1 == '[') {
                if (read(STDIN_FILENO, &s2, 1) != 1) break;
                switch (s2) {
                case 'A':  /* 上箭头：历史上一行 */
                    if (hist_index + 1 < msh_history_count()) {
                        /* 第一次进入历史时保存当前缓冲 */
                        if (hist_index == -1) {
                            free(saved_buf);
                            saved_buf = malloc(len + 1);
                            memcpy(saved_buf, buf, len + 1);
                            saved_len = len;
                        }
                        hist_index++;
                        const char *h = msh_history_get(msh_history_count() - 1 - hist_index);
                        if (h) {
                            emit_str("\033[2K\r");
                            if (prompt) emit_str(prompt);
                            len = strlen(h);
                            if (len + 1 > cap) {
                                cap = len + 1;
                                buf = realloc(buf, cap);
                            }
                            memcpy(buf, h, len + 1);
                            cur = len;
                            emit(buf, len);
                        }
                    }
                    break;
                case 'B':  /* 下箭头：历史下一行 */
                    if (hist_index > 0) {
                        hist_index--;
                        const char *h = msh_history_get(msh_history_count() - 1 - hist_index);
                        emit_str("\033[2K\r");
                        if (prompt) emit_str(prompt);
                        if (h) {
                            len = strlen(h);
                            if (len + 1 > cap) { cap = len + 1; buf = realloc(buf, cap); }
                            memcpy(buf, h, len + 1);
                        } else {
                            len = 0; buf[0] = '\0';
                        }
                        cur = len;
                        emit(buf, len);
                    } else if (hist_index == 0) {
                        /* 回到原始缓冲 */
                        hist_index = -1;
                        emit_str("\033[2K\r");
                        if (prompt) emit_str(prompt);
                        if (saved_buf) {
                            len = saved_len;
                            if (len + 1 > cap) { cap = len + 1; buf = realloc(buf, cap); }
                            memcpy(buf, saved_buf, len + 1);
                        } else {
                            len = 0; buf[0] = '\0';
                        }
                        cur = len;
                        emit(buf, len);
                    }
                    break;
                case 'C':  /* 右箭头 */
                    if (cur < len) {
                        cur = utf8_next_char(buf, cur, len);
                        cursor_right(1);
                    }
                    break;
                case 'D':  /* 左箭头 */
                    if (cur > 0) {
                        cur = utf8_prev_char(buf, cur);
                        cursor_left(1);
                    }
                    break;
                case 'H':  /* Home */
                    if (cur > 0) {
                        size_t chars = utf8_display_width(buf, cur);
                        cursor_left(chars);
                        cur = 0;
                    }
                    break;
                case 'F':  /* End */
                    if (cur < len) {
                        size_t chars = utf8_display_width(buf + cur, len - cur);
                        cursor_right(chars);
                        cur = len;
                    }
                    break;
                case '3':  /* Delete */
                    {
                        char s3;
                        if (read(STDIN_FILENO, &s3, 1) == 1 && s3 == '~') {
                            if (cur < len) {
                                size_t cl = utf8_next_char(buf, cur, len) - cur;
                                memmove(buf + cur, buf + cur + cl, len - cur - cl + 1);
                                len -= cl;
                                emit_str("\033[K");
                                emit(buf + cur, len - cur);
                                size_t chars_after = utf8_display_width(buf + cur, len - cur);
                                cursor_left(chars_after);
                            }
                        }
                    }
                    break;
                /* 支持 \033[1~ (Home) 和 \033[4~ (End) — rxvt 风格 */
                case '1':
                    {
                        char s3;
                        if (read(STDIN_FILENO, &s3, 1) == 1 && s3 == '~') {
                            if (cur > 0) {
                                size_t chars = utf8_display_width(buf, cur);
                                cursor_left(chars);
                                cur = 0;
                            }
                        }
                    }
                    break;
                case '4':
                    {
                        char s3;
                        if (read(STDIN_FILENO, &s3, 1) == 1 && s3 == '~') {
                            if (cur < len) {
                                size_t chars = utf8_display_width(buf + cur, len - cur);
                                cursor_right(chars);
                                cur = len;
                            }
                        }
                    }
                    break;
                }
            } else if (s1 == 'O') {
                /* \033OA 等 — xterm 应用模式 */
                if (read(STDIN_FILENO, &s2, 1) != 1) break;
                switch (s2) {
                case 'A': goto hist_up;   /* 使用下面标签重定向 */
                case 'B': goto hist_down;
                case 'C': goto cur_right;
                case 'D': goto cur_left;
                case 'H': goto go_home;
                case 'F': goto go_end;
                }
                /* 不可能到达，但消除编译器警告 */
                break;
                /* 标签定义在下面 — 通过 goto 跳到对应逻辑 */
                hist_up:
                    if (hist_index + 1 < msh_history_count()) {
                        if (hist_index == -1) {
                            free(saved_buf);
                            saved_buf = malloc(len + 1);
                            memcpy(saved_buf, buf, len + 1);
                            saved_len = len;
                        }
                        hist_index++;
                        const char *h = msh_history_get(msh_history_count() - 1 - hist_index);
                        if (h) {
                            emit_str("\033[2K\r");
                            if (prompt) emit_str(prompt);
                            len = strlen(h);
                            if (len + 1 > cap) { cap = len + 1; buf = realloc(buf, cap); }
                            memcpy(buf, h, len + 1);
                            cur = len;
                            emit(buf, len);
                        }
                    }
                    break;
                hist_down:
                    if (hist_index > 0) {
                        hist_index--;
                        const char *h = msh_history_get(msh_history_count() - 1 - hist_index);
                        emit_str("\033[2K\r");
                        if (prompt) emit_str(prompt);
                        if (h) {
                            len = strlen(h);
                            if (len + 1 > cap) { cap = len + 1; buf = realloc(buf, cap); }
                            memcpy(buf, h, len + 1);
                        } else {
                            len = 0; buf[0] = '\0';
                        }
                        cur = len;
                        emit(buf, len);
                    } else if (hist_index == 0) {
                        hist_index = -1;
                        emit_str("\033[2K\r");
                        if (prompt) emit_str(prompt);
                        if (saved_buf) {
                            len = saved_len;
                            if (len + 1 > cap) { cap = len + 1; buf = realloc(buf, cap); }
                            memcpy(buf, saved_buf, len + 1);
                        } else {
                            len = 0; buf[0] = '\0';
                        }
                        cur = len;
                        emit(buf, len);
                    }
                    break;
                cur_right:
                    if (cur < len) {
                        cur = utf8_next_char(buf, cur, len);
                        cursor_right(1);
                    }
                    break;
                cur_left:
                    if (cur > 0) {
                        cur = utf8_prev_char(buf, cur);
                        cursor_left(1);
                    }
                    break;
                go_home:
                    if (cur > 0) {
                        size_t chars = utf8_display_width(buf, cur);
                        cursor_left(chars);
                        cur = 0;
                    }
                    break;
                go_end:
                    if (cur < len) {
                        size_t chars = utf8_display_width(buf + cur, len - cur);
                        cursor_right(chars);
                        cur = len;
                    }
                    break;
            }
            continue;
        }

        /* ── Tab 补全 ── */
        if (c == 9) {
            if (len + 256 >= cap) {
                cap = cap * 2 + 256;
                buf = realloc(buf, cap);
            }
            int prev_was_tab = (hist_index >= 0); /* 简化 */
            int rc = msh_complete(buf, &cur, &len, cap, prev_was_tab);
            if (rc > 0) {
                emit_str("\033[2K\r");
                if (prompt) emit_str(prompt);
                emit(buf, len);
                if (cur < len) {
                    size_t chars_after = utf8_display_width(buf + cur, len - cur);
                    cursor_left(chars_after);
                }
            }
            continue;
        }

        /* ── Backspace ── */
        if (c == 127 || c == 8) {
            if (cur > 0) {
                size_t prev = utf8_prev_char(buf, cur);
                size_t cl = cur - prev;
                memmove(buf + prev, buf + cur, len - cur + 1);
                len -= cl;
                cur = prev;
                /* 重绘：光标退 1 列，清行尾，输出剩余，光标归位 */
                cursor_left(1);
                emit_str("\033[K");
                emit(buf + cur, len - cur);
                size_t chars_after = utf8_display_width(buf + cur, len - cur);
                cursor_left(chars_after);
            }
            continue;
        }

        /* ── Ctrl-A: 行首 ── */
        if (c == 1) {
            if (cur > 0) {
                size_t chars = utf8_display_width(buf, cur);
                cursor_left(chars);
                cur = 0;
            }
            continue;
        }

        /* ── Ctrl-E: 行尾 ── */
        if (c == 5) {
            if (cur < len) {
                size_t chars = utf8_display_width(buf + cur, len - cur);
                cursor_right(chars);
                cur = len;
            }
            continue;
        }

        /* ── Ctrl-B: 左 ── */
        if (c == 2) {
            if (cur > 0) {
                cur = utf8_prev_char(buf, cur);
                cursor_left(1);
            }
            continue;
        }

        /* ── Ctrl-F: 右 ── */
        if (c == 6) {
            if (cur < len) {
                cur = utf8_next_char(buf, cur, len);
                cursor_right(1);
            }
            continue;
        }

        /* ── Ctrl-K: 删到行尾 ── */
        if (c == 11) {
            if (cur < len) {
                len = cur;
                buf[len] = '\0';
                emit_str("\033[K");
            }
            continue;
        }

        /* ── Ctrl-U: 删到行首 ── */
        if (c == 21) {
            if (cur > 0) {
                size_t chars = utf8_display_width(buf, cur);
                memmove(buf, buf + cur, len - cur + 1);
                len -= cur;
                cur = 0;
                cursor_left(chars);
                emit_str("\033[K");
                emit(buf, len);
            }
            continue;
        }

        /* ── Ctrl-W: 删前一个 word ── */
        if (c == 23) {
            if (cur > 0) {
                size_t p = cur;
                /* 跳过空白 */
                while (p > 0 && (buf[p-1] == ' ' || buf[p-1] == '\t')) p = utf8_prev_char(buf, p);
                /* 跳过非空白 */
                while (p > 0 && buf[p-1] != ' ' && buf[p-1] != '\t') p = utf8_prev_char(buf, p);
                size_t deleted = cur - p;
                size_t chars_deleted = utf8_display_width(buf + p, deleted);
                memmove(buf + p, buf + cur, len - cur + 1);
                len -= deleted;
                cur = p;
                cursor_left(chars_deleted);
                emit_str("\033[K");
                emit(buf + cur, len - cur);
                size_t chars_after = utf8_display_width(buf + cur, len - cur);
                cursor_left(chars_after);
            }
            continue;
        }

        /* ── 忽略其他控制字符 ── */
        if (iscntrl((unsigned char)c)) continue;

        /* ── 普通字符 / UTF-8 多字节字符插入 ── */
        {
            /* 读取完整的 UTF-8 字符 */
            int cl = utf8_char_len((unsigned char)c);
            char chars[4];
            chars[0] = c;
            for (int i = 1; i < cl; i++) {
                if (read(STDIN_FILENO, &chars[i], 1) != 1) {
                    /* 读取续字节失败，退化为已读部分 */
                    cl = i;
                    break;
                }
            }

            if (len + cl + 1 >= cap) {
                cap = (cap + cl + 1) * 2;
                buf = realloc(buf, cap);
            }

            memmove(buf + cur + cl, buf + cur, len - cur + 1);
            memcpy(buf + cur, chars, cl);
            cur += cl;
            len += cl;

            /* 重绘从插入点起 */
            emit(buf + cur - cl, len - (cur - cl));
            /* 光标归位到 cur */
            if (cur < len) {
                size_t chars_after = utf8_display_width(buf + cur, len - cur);
                cursor_left(chars_after);
            }
        }
    }

    buf[len] = '\0';
    free(saved_buf);
    term_restore();
    return buf;
}
