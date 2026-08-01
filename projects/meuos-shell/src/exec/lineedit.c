/* msh/exec/lineedit.c — 行编辑（裸 termios，不依赖 readline）
 *
 * 功能：
 *   - 字符级编辑：左右移动、Home/End、Backspace/Delete
 *   - 上下箭头翻历史
 *   - Ctrl-A/E（行首/行尾）、Ctrl-C 中断、Ctrl-D 空行 EOF
 *   - 输出到 stdout（提示符由调用者绘制或内部绘制）
 *
 * 今日不做：Tab 补全、vi/emacs 模式切换、增量搜索。
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "msh/history.h"
#include "msh/complete.h"

/* 读取一个编辑好的行。prompt 非 NULL 时先输出 prompt。
 * 返回 malloc 字符串（不含换行），EOF 返回 NULL。 */
char *msh_readline(const char *prompt) {
    /* 保存终端状态 */
    struct termios orig, raw;
    if (tcgetattr(STDIN_FILENO, &orig) < 0) {
        /* 非 tty：退化为 getline */
        if (prompt) { fputs(prompt, stdout); fflush(stdout); }
        char *line = NULL;
        size_t cap = 0;
        ssize_t n = getline(&line, &cap, stdin);
        if (n < 0) { free(line); return NULL; }
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
        return line;
    }
    raw = orig;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) return NULL;

    char *buf = malloc(256);
    size_t cap = 256;
    size_t len = 0;      /* 当前行长度 */
    size_t cur = 0;      /* 光标位置 */
    (void)0; /* hist_pos removed - history navigation uses hist_index directly */
    int hist_index = -1; /* -1 表示不在历史中 */
    int prev_was_tab = 0; /* 上一次按键是否是 Tab */

    if (prompt) { fputs(prompt, stdout); fflush(stdout); }

    for (;;) {
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1) break;

        if (c == 3) {          /* Ctrl-C */
            /* 清行，中断 */
            fprintf(stdout, "^C\n");
            fflush(stdout);
            len = 0; cur = 0;
            buf[0] = '\0';
            /* 恢复终端 */
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
            /* 返回一个特殊标记行 */
            return strdup("\003");
        }
        if (c == 4) {          /* Ctrl-D */
            if (len == 0) {
                /* 空行 EOF */
                tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
                free(buf);
                return NULL;
            }
            /* 删除光标处字符 */
            if (cur < len) {
                memmove(buf + cur, buf + cur + 1, len - cur);
                len--;
                /* 重绘 */
                fputs("\033[K", stdout);
                fputs(buf + cur, stdout);
                fputs("\033[", stdout);
                printf("%zuD", len - cur);
                fflush(stdout);
            }
            continue;
        }
        if (c == 10 || c == 13) {  /* Enter */
            fprintf(stdout, "\n");
            fflush(stdout);
            break;
        }
        if (c == 27) {         /* ESC 序列 */
            char s1, s2;
            if (read(STDIN_FILENO, &s1, 1) != 1) break;
            if (s1 == '[') {
                if (read(STDIN_FILENO, &s2, 1) != 1) break;
                switch (s2) {
                case 'A':  /* 上箭头：历史上一行 */
                    if (hist_index + 1 < msh_history_count()) {
                        hist_index++;
                        const char *h = msh_history_get(msh_history_count() - 1 - hist_index);
                        if (h) {
                            /* 清当前行 */
                            fprintf(stdout, "\033[2K\r");
                            if (prompt) fputs(prompt, stdout);
                            len = strlen(h);
                            if (len + 1 > cap) {
                                cap = len + 1;
                                buf = realloc(buf, cap);
                            }
                            memcpy(buf, h, len + 1);
                            cur = len;
                            fputs(buf, stdout);
                            fflush(stdout);
                        }
                    }
                    break;
                case 'B':  /* 下箭头：历史下一行 */
                    if (hist_index > 0) {
                        hist_index--;
                        const char *h = msh_history_get(msh_history_count() - 1 - hist_index);
                        fprintf(stdout, "\033[2K\r");
                        if (prompt) fputs(prompt, stdout);
                        if (h) {
                            len = strlen(h);
                            if (len + 1 > cap) { cap = len + 1; buf = realloc(buf, cap); }
                            memcpy(buf, h, len + 1);
                        } else {
                            len = 0; buf[0] = '\0';
                        }
                        cur = len;
                        fputs(buf, stdout);
                        fflush(stdout);
                    } else if (hist_index == 0) {
                        /* 回到空行 */
                        hist_index = -1;
                        fprintf(stdout, "\033[2K\r");
                        if (prompt) fputs(prompt, stdout);
                        len = 0; cur = 0; buf[0] = '\0';
                        fflush(stdout);
                    }
                    break;
                case 'C':  /* 右箭头 */
                    if (cur < len) {
                        cur++;
                        fputs("\033[C", stdout);
                        fflush(stdout);
                    }
                    break;
                case 'D':  /* 左箭头 */
                    if (cur > 0) {
                        cur--;
                        fputs("\033[D", stdout);
                        fflush(stdout);
                    }
                    break;
                case 'H':  /* Home */
                    if (cur > 0) {
                        fputs("\033[", stdout);
                        printf("%zuD", cur);
                        cur = 0;
                        fflush(stdout);
                    }
                    break;
                case 'F':  /* End */
                    if (cur < len) {
                        fputs("\033[", stdout);
                        printf("%zuC", len - cur);
                        cur = len;
                        fflush(stdout);
                    }
                    break;
                case '3':  /* Delete */
                    {
                        char s3;
                        if (read(STDIN_FILENO, &s3, 1) == 1 && s3 == '~') {
                            if (cur < len) {
                                memmove(buf + cur, buf + cur + 1, len - cur);
                                len--;
                                fputs("\033[K", stdout);
                                fputs(buf + cur, stdout);
                                fputs("\033[", stdout);
                                printf("%zuD", len - cur);
                                fflush(stdout);
                            }
                        }
                    }
                    break;
                }
            }
            continue;
        }
        if (c == 9) {  /* Tab 补全 */
            if (len + 256 >= cap) {
                cap = cap * 2 + 256;
                buf = realloc(buf, cap);
            }
            int rc = msh_complete(buf, &cur, &len, cap, prev_was_tab);
            if (rc > 0) {
                fprintf(stdout, "\033[2K\r");
                if (prompt) fputs(prompt, stdout);
                fputs(buf, stdout);
                if (cur < len) {
                    fputs("\033[", stdout);
                    printf("%zuD", len - cur);
                }
                fflush(stdout);
            }
            prev_was_tab = 1;
            continue;
        }
        if (c == 127 || c == 8) {  /* Backspace */
            if (cur > 0) {
                memmove(buf + cur - 1, buf + cur, len - cur + 1);
                len--;
                cur--;
                fputs("\b", stdout);
                fputs("\033[K", stdout);
                fputs(buf + cur, stdout);
                fputs("\033[", stdout);
                printf("%zuD", len - cur);
                fflush(stdout);
            }
            continue;
        }
        if (c == 1) {  /* Ctrl-A: 行首 */
            if (cur > 0) {
                fputs("\033[", stdout);
                printf("%zuD", cur);
                cur = 0;
                fflush(stdout);
            }
            continue;
        }
        if (c == 5) {  /* Ctrl-E: 行尾 */
            if (cur < len) {
                fputs("\033[", stdout);
                printf("%zuC", len - cur);
                cur = len;
                fflush(stdout);
            }
            continue;
        }
        if (c == 2) {  /* Ctrl-B: 左 */
            if (cur > 0) { cur--; fputs("\033[D", stdout); fflush(stdout); }
            continue;
        }
        if (c == 6) {  /* Ctrl-F: 右 */
            if (cur < len) { cur++; fputs("\033[C", stdout); fflush(stdout); }
            continue;
        }
        if (c == 11) {  /* Ctrl-K: 删到行尾 */
            if (cur < len) {
                len = cur;
                buf[len] = '\0';
                fputs("\033[K", stdout);
                fflush(stdout);
            }
            continue;
        }
        if (iscntrl((unsigned char)c)) { prev_was_tab = 0; continue; }

        /* 普通字符插入 */
        prev_was_tab = 0;
        if (len + 1 >= cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
        memmove(buf + cur + 1, buf + cur, len - cur + 1);
        buf[cur++] = (char)c;
        len++;
        /* 重绘从光标起 */
        fputs(buf + cur - 1, stdout);
        fputs("\033[", stdout);
        printf("%zuD", len - cur);
        fflush(stdout);
    }

    buf[len] = '\0';
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
    return buf;
}
