/* libutils/color.c — ANSI 24-bit 颜色 */

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "meuos/color.h"
#include "meuos/utils.h"  /* die() 等 */

/* 全局颜色开关：默认 1（可通过 NO_COLOR 关闭） */
int color_enabled = -1;  /* -1 = 未初始化，自动检测 */

static int tty_support_color = -1;
static int truecolor = -1;

void color_enable(void) { color_enabled = 1; }
void color_disable(void) { color_enabled = 0; }

/* 缓存检测结果避免反复探查。 */
static void detect_once(void) {
    if (tty_support_color >= 0) return;
    /* 默认开：通过 isatty() 决定 */
    tty_support_color = 1;
    truecolor = color_detect_truecolor();
}

int color_detect_truecolor(void) {
    const char *ct = getenv("COLORTERM");
    if (ct && (strstr(ct, "truecolor") || strstr(ct, "24bit"))) return 1;
    /* xterm-kitty + wezterm + iTerm 都默认 truecolor */
    const char *term = getenv("TERM");
    if (term && (strstr(term, "truecolor") || strstr(term, "256color"))) return 1;
    return 0;
}

int color_env_disabled(void) {
    /* https://no-color.org/ */
    if (getenv("NO_COLOR")) return 1;
    return 0;
}

/* 内部：检查颜色是否实际启用 */
static int color_is_on(void) {
    detect_once();
    if (color_enabled == 0) return 0;
    if (color_env_disabled()) return 0;
    if (color_enabled < 0 && !tty_support_color) return 0;
    if (color_enabled == -1 && !isatty(fileno(stdout))) return 0;
    return color_enabled >= 0 ? color_enabled : 1;
}

const char *color_fg(uint8_t r, uint8_t g, uint8_t b) {
    static char buf[32];
    if (!color_is_on()) return "";
    if (truecolor) {
        snprintf(buf, sizeof(buf), "\033[38;2;%d;%d;%dm", r, g, b);
    } else {
        /* 256-color 近似 */
        snprintf(buf, sizeof(buf), "\033[38;5;%dm", 16 + 36*(r/51) + 6*(g/51) + b/51);
    }
    return buf;
}

const char *color_bg(uint8_t r, uint8_t g, uint8_t b) {
    static char buf[32];
    if (!color_is_on()) return "";
    if (truecolor) {
        snprintf(buf, sizeof(buf), "\033[48;2;%d;%d;%dm", r, g, b);
    } else {
        snprintf(buf, sizeof(buf), "\033[48;5;%dm", 16 + 36*(r/51) + 6*(g/51) + b/51);
    }
    return buf;
}

const char *color_256(uint8_t n) {
    static char buf[16];
    if (!color_is_on()) return "";
    snprintf(buf, sizeof(buf), "\033[38;5;%dm", n);
    return buf;
}

const char *color_named(int c) {
    /* 标准 16 色 ANSI。c ∈ [0, 15] */
    static const char *map[16] = {
        "\033[30m", "\033[31m", "\033[32m", "\033[33m",
        "\033[34m", "\033[35m", "\033[36m", "\033[37m",
        "\033[90m", "\033[91m", "\033[92m", "\033[93m",
        "\033[94m", "\033[95m", "\033[96m", "\033[97m",
    };
    if (c < 0 || c > 15) return "";
    return color_is_on() ? map[c] : "";
}

const char *color_reset(void) {
    return color_is_on() ? "\033[0m" : "";
}
