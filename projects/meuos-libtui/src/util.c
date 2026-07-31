/* util.c — 低层工具函数
 *
 * 矩形校验、CJK 宽度、空格/分隔线/边框、默认主题调色板。
 * 全部为 24-bit 真彩色透明实现，可被任意 widget 复用。
 */

#define _XOPEN_SOURCE 700

#include "meuos/libtui.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>

/* ══════════════════════════════════════════════════════
 *  默认主题
 * ══════════════════════════════════════════════════════ */

const tui_palette_t tui_meuos_theme = {
    .accent    = TUI_COLOR_GREEN,
    .bg        = TUI_COLOR_DEFAULT,
    .fg        = TUI_COLOR_DEFAULT,
    .border    = TUI_COLOR_GREEN,
    .highlight = TUI_COLOR_GREEN,
    .dim       = TUI_COLOR_BLACK,
    .success   = TUI_COLOR_GREEN,
    .warning   = TUI_COLOR_YELLOW,
    .error     = TUI_COLOR_RED,
    .info      = TUI_COLOR_CYAN,
};

/* ══════════════════════════════════════════════════════
 *  tui_rect_t
 * ══════════════════════════════════════════════════════ */

int tui_rect_valid(const tui_rect_t *r)
{
    return r && r->rows > 0 && r->cols > 0;
}

/* ══════════════════════════════════════════════════════
 *  CJK / Unicode 宽度
 * ══════════════════════════════════════════════════════ */

/* 从 UTF-8 解码一个 codepoint，返回字节数 */
static int utf8_decode(const char *s, unsigned *cp)
{
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) {
        *cp = c;
        return 1;
    } else if (c < 0xE0) {
        *cp = c & 0x1F;
        *cp = (*cp << 6) | ((unsigned char)s[1] & 0x3F);
        return 2;
    } else if (c < 0xF0) {
        *cp = c & 0x0F;
        *cp = (*cp << 6) | ((unsigned char)s[1] & 0x3F);
        *cp = (*cp << 6) | ((unsigned char)s[2] & 0x3F);
        return 3;
    } else {
        *cp = c & 0x07;
        *cp = (*cp << 6) | ((unsigned char)s[1] & 0x3F);
        *cp = (*cp << 6) | ((unsigned char)s[2] & 0x3F);
        *cp = (*cp << 6) | ((unsigned char)s[3] & 0x3F);
        return 4;
    }
}

/* 判断 codepoint 是否为 CJK 双宽字符 */
static int is_wide(unsigned cp)
{
    return (cp == 0x3000) ||                      /* 表意空格 */
           (cp >= 0x1100 && cp <= 0x115F) ||      /* 谚文 Jamo */
           (cp >= 0x2E80 && cp <= 0x303E) ||      /* 部首/康熙/符号 */
           (cp >= 0x3040 && cp <= 0x309F) ||      /* 平假名 */
           (cp >= 0x30A0 && cp <= 0x30FF) ||      /* 片假名 */
           (cp >= 0x3100 && cp <= 0x312F) ||      /* 注音 */
           (cp >= 0x3130 && cp <= 0x318F) ||      /* 谚文兼容 */
           (cp >= 0x3190 && cp <= 0x31FF) ||      /* 谚文/注音扩展 */
           (cp >= 0x3200 && cp <= 0x33FF) ||      /* 中日韩兼容 */
           (cp >= 0x3400 && cp <= 0x4DBF) ||      /* 扩展A */
           (cp >= 0x4E00 && cp <= 0x9FFF) ||      /* 统一表意 */
           (cp >= 0xAC00 && cp <= 0xD7AF) ||      /* 谚文音节 */
           (cp >= 0xF900 && cp <= 0xFAFF) ||      /* 兼容表意 */
           (cp >= 0xFE30 && cp <= 0xFE6F) ||      /* 兼容形式 */
           (cp >= 0xFF01 && cp <= 0xFF60) ||      /* 全角形式 */
           (cp >= 0xFFE0 && cp <= 0xFFE6) ||      /* 全角符号 */
           (cp >= 0x1F200 && cp <= 0x1F2FF) ||    /* 补充表意 */
           (cp >= 0x20000 && cp <= 0x2FFFF) ||    /* 扩展B~F */
           (cp >= 0x30000 && cp <= 0x3FFFF);      /* 扩展G~H */
}

int tui_strwidth(const char *s)
{
    int w = 0;
    while (*s) {
        unsigned cp;
        int bytes = utf8_decode(s, &cp);
        w += is_wide(cp) ? 2 : 1;
        s += bytes;
    }
    return w;
}

int tui_truncate(const char *s, int max_cols)
{
    int w = 0, i = 0;
    while (s[i]) {
        unsigned cp;
        int bytes = utf8_decode(s + i, &cp);
        int cw = is_wide(cp) ? 2 : 1;
        if (w + cw > max_cols) break;
        w += cw;
        i += bytes;
    }
    return i;  /* 返回可安全写入的字节数 */
}

/* ══════════════════════════════════════════════════════
 *  辅助输出
 * ══════════════════════════════════════════════════════ */

int tui_spaces(int fd, int n)
{
    static const char spaces[64] =
        "                                                               ";
    while (n > 0) {
        int chunk = n > 64 ? 64 : n;
        if (write(fd, spaces, (size_t)chunk) != chunk) return TUI_ERR_IO;
        n -= chunk;
    }
    return TUI_OK;
}

int tui_hline(int fd, int col, int width, char ch, tui_color_t color)
{
    if (width <= 0) return TUI_OK;

    char buf[256];
    int  max = width < 255 ? width : 255;
    memset(buf, ch, (size_t)max);
    buf[max] = '\0';

    /* 如果指定颜色则设置 */
    if (color != TUI_COLOR_DEFAULT) {
        char esc[16];
        int n = snprintf(esc, sizeof(esc), "\033[%dm", 30 + (int)color);
        if (write(fd, esc, (size_t)n) != n) return TUI_ERR_IO;
    }

    if (write(fd, buf, (size_t)max) != max) return TUI_ERR_IO;

    if (color != TUI_COLOR_DEFAULT)
        if (write(fd, "\033[0m", 4) != 4) return TUI_ERR_IO;

    (void)col;
    return TUI_OK;
}

int tui_cprintf(int fd, tui_color_t fg, tui_color_t bg, const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    int n;

    /* 设置颜色 */
    char esc_fg[16], esc_bg[16];
    int len_fg = snprintf(esc_fg, sizeof(esc_fg), "\033[%dm", 30 + (int)fg);
    int len_bg = snprintf(esc_bg, sizeof(esc_bg), "\033[%dm", 40 + (int)bg);

    if (fg != TUI_COLOR_DEFAULT)
        write(fd, esc_fg, (size_t)len_fg);
    if (bg != TUI_COLOR_DEFAULT)
        write(fd, esc_bg, (size_t)len_bg);

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0) return TUI_ERR_PARAM;
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
    if (write(fd, buf, (size_t)n) != n) return TUI_ERR_IO;

    /* 重置 */
    if (write(fd, "\033[0m", 4) != 4) return TUI_ERR_IO;

    return TUI_OK;
}

/* ══════════════════════════════════════════════════════
 *  填充矩形
 * ══════════════════════════════════════════════════════ */

int tui_fill_rect(int fd, tui_rect_t area, tui_color_t bg)
{
    int r;
    char esc[16];
    int esc_n = snprintf(esc, sizeof(esc), "\033[%dm", 40 + (int)bg);

    if (esc_n <= 0) return TUI_ERR_PARAM;

    for (r = 0; r < area.rows; r++) {
        tui_cursor_goto(fd, area.row + r, area.col);
        if (write(fd, esc, (size_t)esc_n) != esc_n) return TUI_ERR_IO;
        if (tui_spaces(fd, area.cols) != TUI_OK) return TUI_ERR_IO;
    }

    tui_reset_style(fd);
    return TUI_OK;
}

/* ══════════════════════════════════════════════════════
 *  边框绘制 (面板)
 * ══════════════════════════════════════════════════════ */

/* 边框字符集 */
static const char *box_chars[4][6] = {
    /* 单线 */ { "─", "│", "┌", "┐", "└", "┘" },
    /* 双线 */ { "═", "║", "╔", "╗", "╚", "╝" },
    /* 圆角 */ { "─", "│", "╭", "╮", "╰", "╯" },
    /* 粗  */ { "━", "┃", "┏", "┓", "┗", "┛" },
};

/* UTF-8 字符串长度（字节数） */
#define BCLEN 3  /* 所有框线字符均为 3 字节 UTF-8 */

/* 绘制边框（公开 API，用于面板/对话框） */
int tui_draw_border(int fd, tui_rect_t *inner,
                    const char *title, int style_idx,
                    tui_color_t border_color)
{
    const char **box = box_chars[style_idx & 3];
    int r, left = 0;

    if (inner->rows < 3 || inner->cols < 4) return TUI_OK;

    /* 背景填充 */
    tui_set_bg(fd, TUI_COLOR_DEFAULT);

    /* 设置边框颜色 */
    char esc[16];
    snprintf(esc, sizeof(esc), "\033[%dm", 30 + (int)border_color);
    write(fd, esc, strlen(esc));

    tui_set_attr(fd, TUI_ATTR_BOLD);

    /* ── 上边框 ── */
    tui_cursor_goto(fd, inner->row, inner->col);
    write(fd, box[2], BCLEN);                  /* ┌/╔/╭/┏ */

    if (title && title[0]) {
        write(fd, " ", 1);
        tui_set_attr(fd, TUI_ATTR_RESET);
        tui_set_fg(fd, border_color);
        write(fd, title, strlen(title));
        tui_set_attr(fd, TUI_ATTR_BOLD);
        left = 2 + tui_strwidth(title);
    }

    /* 上边框水平线：手动写入 UTF-8 字符循环 */
    {
        int i, hw = inner->cols - left - 1;
        tui_cursor_goto(fd, inner->row, inner->col + left);
        snprintf(esc, sizeof(esc), "\033[%dm", 30 + (int)border_color);
        write(fd, esc, strlen(esc));
        tui_set_attr(fd, TUI_ATTR_BOLD);
        for (i = 0; i < hw; i++)
            write(fd, box[0], BCLEN);
    }

    /* 行尾 ┐/╗/╮/┓ */
    tui_cursor_goto(fd, inner->row, inner->col + inner->cols - 1);
    write(fd, box[3], BCLEN);

    /* ── 下边框 ── */
    tui_cursor_goto(fd, inner->row + inner->rows - 1, inner->col);
    write(fd, box[4], BCLEN);                  /* └/╚/╰/┗ */

    {
        int i, hw = inner->cols - 2;
        tui_cursor_goto(fd, inner->row + inner->rows - 1, inner->col + 1);
        for (i = 0; i < hw; i++)
            write(fd, box[0], BCLEN);
    }

    tui_cursor_goto(fd, inner->row + inner->rows - 1, inner->col + inner->cols - 1);
    write(fd, box[5], BCLEN);                  /* ┘/╝/╯/┛ */

    /* ── 竖边框 ── */
    for (r = 1; r < inner->rows - 1; r++) {
        tui_cursor_goto(fd, inner->row + r, inner->col);
        write(fd, box[1], BCLEN);
        tui_cursor_goto(fd, inner->row + r, inner->col + inner->cols - 1);
        write(fd, box[1], BCLEN);
    }

    tui_reset_style(fd);

    /* 更新 inner 为内容区域 */
    inner->row    += 1;
    inner->rows   -= 2;
    inner->col    += 1;
    inner->cols   -= 2;

    return TUI_OK;
}

