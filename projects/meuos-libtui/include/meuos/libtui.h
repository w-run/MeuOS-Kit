#ifndef MEUOS_LIBTUI_H
#define MEUOS_LIBTUI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 错误码 ───────────────────────────────────────── */

#define TUI_OK          0
#define TUI_ERR_IO      (-1)  /* I/O 错误 */
#define TUI_ERR_PARAM   (-2)  /* 参数错误 */
#define TUI_ERR_MEM     (-3)  /* 内存分配失败 */
#define TUI_ERR_NOTTY   (-4)  /* 不是终端 */

/* ── 键盘码 ───────────────────────────────────────── */

typedef enum {
    /* ASCII 控制字符 (0x00-0x1F) */
    TUI_KEY_NUL      = 0x00,
    TUI_KEY_CTRL_A   = 0x01,
    TUI_KEY_CTRL_B   = 0x02,
    TUI_KEY_CTRL_C   = 0x03,
    TUI_KEY_CTRL_D   = 0x04,
    TUI_KEY_CTRL_E   = 0x05,
    TUI_KEY_CTRL_F   = 0x06,
    TUI_KEY_BELL     = 0x07,
    TUI_KEY_BS       = 0x08,  /* Backspace */
    TUI_KEY_TAB      = 0x09,
    TUI_KEY_LF       = 0x0A,  /* Line Feed / Enter */
    TUI_KEY_CTRL_K   = 0x0B,
    TUI_KEY_CTRL_L   = 0x0C,
    TUI_KEY_CR       = 0x0D,  /* Carriage Return */
    TUI_KEY_CTRL_N   = 0x0E,
    TUI_KEY_CTRL_O   = 0x0F,
    TUI_KEY_CTRL_P   = 0x10,
    TUI_KEY_CTRL_Q   = 0x11,
    TUI_KEY_CTRL_R   = 0x12,
    TUI_KEY_CTRL_S   = 0x13,
    TUI_KEY_CTRL_T   = 0x14,
    TUI_KEY_CTRL_U   = 0x15,
    TUI_KEY_CTRL_V   = 0x16,
    TUI_KEY_CTRL_W   = 0x17,
    TUI_KEY_CTRL_X   = 0x18,
    TUI_KEY_CTRL_Y   = 0x19,
    TUI_KEY_CTRL_Z   = 0x1A,
    TUI_KEY_ESC      = 0x1B,
    TUI_KEY_DEL      = 0x7F,

    /* 功能键和方向键 (>= 0x1000) */
    TUI_KEY_UP       = 0x1000,
    TUI_KEY_DOWN     = 0x1001,
    TUI_KEY_LEFT     = 0x1002,
    TUI_KEY_RIGHT    = 0x1003,
    TUI_KEY_HOME     = 0x1004,
    TUI_KEY_END      = 0x1005,
    TUI_KEY_PGUP     = 0x1006,
    TUI_KEY_PGDN     = 0x1007,
    TUI_KEY_INS      = 0x1008,

    TUI_KEY_F1       = 0x1100,
    TUI_KEY_F2       = 0x1101,
    TUI_KEY_F3       = 0x1102,
    TUI_KEY_F4       = 0x1103,
    TUI_KEY_F5       = 0x1104,
    TUI_KEY_F6       = 0x1105,
    TUI_KEY_F7       = 0x1106,
    TUI_KEY_F8       = 0x1107,
    TUI_KEY_F9       = 0x1108,
    TUI_KEY_F10      = 0x1109,
    TUI_KEY_F11      = 0x110A,
    TUI_KEY_F12      = 0x110B,

    /* 修饰键组合 */
    TUI_KEY_S_TAB     = 0x1200,  /* Shift+Tab */

    /* 特殊事件 */
    TUI_KEY_TIMEOUT  = 0x1FFF,  /* 超时无输入 */
    TUI_KEY_RESIZE   = 0x1FFE,  /* 窗口大小变化 */
    TUI_KEY_MOUSE    = 0x1FFD,  /* 鼠标事件 */
    TUI_KEY_ERR      = 0x1FFC,  /* 输入错误 */
} tui_key_t;

/* ── 鼠标事件 ─────────────────────────────────────── */

typedef struct {
    int x, y;       /* 列/行 (1-based) */
    int button;     /* 0=左,1=中,2=右,64=移动 */
    int pressed;    /* 1=按下, 0=释放 */
} tui_mouse_t;

/* ── 输入事件 ─────────────────────────────────────── */

typedef struct {
    tui_key_t   key;     /* 键盘码或 TUI_KEY_MOUSE */
    tui_mouse_t mouse;   /* 鼠标详情 (仅 key==TUI_KEY_MOUSE) */
} tui_event_t;

/* ── 颜色 ─────────────────────────────────────────── */

typedef enum {
    TUI_COLOR_BLACK   = 0,
    TUI_COLOR_RED,
    TUI_COLOR_GREEN,
    TUI_COLOR_YELLOW,
    TUI_COLOR_BLUE,
    TUI_COLOR_MAGENTA,
    TUI_COLOR_CYAN,
    TUI_COLOR_WHITE,
    TUI_COLOR_DEFAULT = 9,
} tui_color_t;

/* ── 样式属性 ─────────────────────────────────────── */

typedef enum {
    TUI_ATTR_RESET      = 0,
    TUI_ATTR_BOLD       = 1,
    TUI_ATTR_DIM        = 2,
    TUI_ATTR_ITALIC     = 3,
    TUI_ATTR_UNDERLINE  = 4,
    TUI_ATTR_BLINK      = 5,
    TUI_ATTR_REVERSE    = 7,
    TUI_ATTR_HIDDEN     = 8,
    TUI_ATTR_STRIKE     = 9,
} tui_attr_t;

/* ── 终端尺寸 ─────────────────────────────────────── */

typedef struct {
    int rows;       /* 行数 */
    int cols;       /* 列数 */
    int xpixel;     /* 像素宽度 (可能为0) */
    int ypixel;     /* 像素高度 (可能为0) */
} tui_size_t;

/* ── SIGWINCH 回调 ────────────────────────────────── */

typedef void (*tui_resize_cb)(tui_size_t size, void *userdata);

/* ══════════════════════════════════════════════════════
 *  terminal.c — 终端 I/O
 * ══════════════════════════════════════════════════════ */

/* 进入/退出原始模式。返回 TUI_OK 或 TUI_ERR_* */
int tui_raw_mode(int fd, int enable);

/* 进入/退出备用屏幕缓冲区 */
int tui_alt_screen(int fd, int enable);

/* 启用/禁用 XTerm SGR 鼠标跟踪 */
int tui_mouse(int fd, int enable);

/* 注册 SIGWINCH 回调 (NULL 取消注册) */
int tui_on_resize(tui_resize_cb cb, void *userdata);

/* ══════════════════════════════════════════════════════
 *  screen.c — 屏幕操作
 * ══════════════════════════════════════════════════════ */

/* 获取终端尺寸 (TIOCGWINSZ, 写入 size) */
int tui_get_size(int fd, tui_size_t *size);

/* 光标定位 (1-based row, col) */
int tui_cursor_goto(int fd, int row, int col);
int tui_cursor_up(int fd, int n);
int tui_cursor_down(int fd, int n);
int tui_cursor_left(int fd, int n);
int tui_cursor_right(int fd, int n);

/* 保存/恢复光标位置 */
int tui_cursor_save(int fd);
int tui_cursor_restore(int fd);

/* 显示/隐藏光标 */
int tui_cursor_show(int fd, int show);

/* 清除屏幕 */
int tui_clear_screen(int fd);      /* 全屏清空 */
int tui_clear_line(int fd);        /* 整行清空 */
int tui_clear_eol(int fd);         /* 行尾清空 */

/* 设置颜色和样式 */
int tui_set_fg(int fd, tui_color_t c);
int tui_set_bg(int fd, tui_color_t c);
int tui_set_attr(int fd, tui_attr_t a);

/* 重置所有颜色和样式为默认 */
int tui_reset_style(int fd);

/* 写入带样式的文本 (printf 风格) */
int tui_printf(int fd, const char *fmt, ...);

/* ══════════════════════════════════════════════════════
 *  input.c — 输入解析
 * ══════════════════════════════════════════════════════ */

/* 阻塞读取一个按键事件 */
int tui_getkey(int fd, tui_event_t *ev);

/* 带超时读取 (timeout_ms: 毫秒, 0=立即返回) */
int tui_getkey_timeout(int fd, tui_event_t *ev, int timeout_ms);

/* 回显字符 (直接 write) */
int tui_putchar(int fd, char c);

/* 写入字符串 */
int tui_write(int fd, const char *s);

/* 刷新输出缓冲区 */
int tui_flush(int fd);

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_LIBTUI_H */
