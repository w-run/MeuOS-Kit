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
    TUI_KEY_BS       = 0x08,
    TUI_KEY_TAB      = 0x09,
    TUI_KEY_LF       = 0x0A,
    TUI_KEY_CTRL_K   = 0x0B,
    TUI_KEY_CTRL_L   = 0x0C,
    TUI_KEY_CR       = 0x0D,
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
    TUI_KEY_S_TAB     = 0x1200,

    /* 特殊事件 */
    TUI_KEY_TIMEOUT  = 0x1FFF,
    TUI_KEY_RESIZE   = 0x1FFE,
    TUI_KEY_MOUSE    = 0x1FFD,
    TUI_KEY_ERR      = 0x1FFC,
} tui_key_t;

/* ── 鼠标事件 ─────────────────────────────────────── */

typedef struct {
    int x, y;
    int button;
    int pressed;
} tui_mouse_t;

/* ── 输入事件 ─────────────────────────────────────── */

typedef struct {
    tui_key_t   key;
    tui_mouse_t mouse;
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
    int rows;
    int cols;
    int xpixel;
    int ypixel;
} tui_size_t;

/* ── SIGWINCH 回调 ────────────────────────────────── */

typedef void (*tui_resize_cb)(tui_size_t size, void *userdata);

/* ══════════════════════════════════════════════════════
 *  terminal.c — 终端 I/O
 * ══════════════════════════════════════════════════════ */

int tui_raw_mode(int fd, int enable);
int tui_alt_screen(int fd, int enable);
int tui_mouse(int fd, int enable);
int tui_on_resize(tui_resize_cb cb, void *userdata);

/* ══════════════════════════════════════════════════════
 *  screen.c — 屏幕操作
 * ══════════════════════════════════════════════════════ */

int tui_get_size(int fd, tui_size_t *size);
int tui_cursor_goto(int fd, int row, int col);
int tui_cursor_up(int fd, int n);
int tui_cursor_down(int fd, int n);
int tui_cursor_left(int fd, int n);
int tui_cursor_right(int fd, int n);
int tui_cursor_save(int fd);
int tui_cursor_restore(int fd);
int tui_cursor_show(int fd, int show);
int tui_clear_screen(int fd);
int tui_clear_line(int fd);
int tui_clear_eol(int fd);
int tui_set_fg(int fd, tui_color_t c);
int tui_set_bg(int fd, tui_color_t c);
int tui_set_attr(int fd, tui_attr_t a);
int tui_reset_style(int fd);
int tui_printf(int fd, const char *fmt, ...);

/* ══════════════════════════════════════════════════════
 *  input.c — 输入解析
 * ══════════════════════════════════════════════════════ */

int tui_getkey(int fd, tui_event_t *ev);
int tui_getkey_timeout(int fd, tui_event_t *ev, int timeout_ms);
int tui_putchar(int fd, char c);
int tui_write(int fd, const char *s);
int tui_flush(int fd);

/* ══════════════════════════════════════════════════════
 *  tui_rect_t — 矩形区域
 * ══════════════════════════════════════════════════════ */

typedef struct {
    int row, col;       /* 左上角 (1-based) */
    int rows, cols;     /* 尺寸 */
} tui_rect_t;

/* 判断矩形是否有效 (有内容) */
int  tui_rect_valid(const tui_rect_t *r);

/* ══════════════════════════════════════════════════════
 *  主题/调色板
 * ══════════════════════════════════════════════════════ */

typedef struct {
    tui_color_t accent;      /* MeuOS 主题绿 (主色调) */
    tui_color_t bg;          /* 默认背景 */
    tui_color_t fg;          /* 默认前景 */
    tui_color_t border;      /* 边框颜色 */
    tui_color_t highlight;   /* 高亮/选中 */
    tui_color_t dim;         /* 弱化文本 */
    tui_color_t success;     /* 成功/正向 (绿色) */
    tui_color_t warning;     /* 警告 (黄色) */
    tui_color_t error;       /* 错误 (红色) */
    tui_color_t info;        /* 信息 (青色) */
} tui_palette_t;

/* MeuOS 默认主题：柔和绿底白字调色板 */
extern const tui_palette_t tui_meuos_theme;

/* ══════════════════════════════════════════════════════
 *  24-bit 真彩色支持 (xterm-256color / iTerm2 / Windows Terminal)
 * ══════════════════════════════════════════════════════ */

typedef struct { uint8_t r, g, b; } tui_rgb_t;

#define TUI_RGB(r,g,b) ((tui_rgb_t){(r),(g),(b)})

/* 直接以 RGB 写入前景/背景 ANSI 转义（24-bit color） */
int tui_set_fg_rgb(int fd, tui_rgb_t c);
int tui_set_bg_rgb(int fd, tui_rgb_t c);

/* 16 色 → 近似 RGB（用于主题渲染/PNG 截图） */
tui_rgb_t tui_color_to_rgb(tui_color_t c);
tui_rgb_t tui_color_to_rgb_xterm(tui_color_t c);  /* 更准确 xterm-256 调色板 */

/* ══════════════════════════════════════════════════════
 *  主题系统 (themes.c)
 * ══════════════════════════════════════════════════════ */

/* 完整主题定义：包含 16 色调色板 + 装饰色 + 24-bit 渐变 */
typedef struct {
    const char   *name;            /* 主题名 */
    tui_palette_t palette;         /* 基础 16 色 */
    tui_rgb_t     surface_bg;      /* 内容区背景 */
    tui_rgb_t     surface_fg;      /* 内容区前景 */
    tui_rgb_t     header_bg;       /* 顶栏背景 */
    tui_rgb_t     header_fg;       /* 顶栏前景 */
    tui_rgb_t     statusbar_bg;    /* 底栏背景 */
    tui_rgb_t     statusbar_fg;    /* 底栏前景 */
    tui_rgb_t     gradient[6];     /* 标题渐变色 (0..5 渐变到主色) */
    tui_rgb_t     selection_bg;    /* 选中行背景 */
    tui_rgb_t     selection_fg;    /* 选中行前景 */
    tui_rgb_t     zebra_bg;        /* 表格斑马纹背景 */
    int           use_24bit;       /* 是否启用 24-bit (xterm-256color 模拟为 16) */
} tui_theme_t;

/* 预设主题 */
extern const tui_theme_t tui_theme_meuos;         /* 默认 MeuOS 绿调 */
extern const tui_theme_t tui_theme_solarized;     /* Solarized Dark */
extern const tui_theme_t tui_theme_nord;          /* Nord */
extern const tui_theme_t tui_theme_catppuccin;    /* Catppuccin Mocha */
extern const tui_theme_t tui_theme_cyberpunk;     /* Cyberpunk 2077 风格 */
extern const tui_theme_t tui_theme_mono;          /* 极简单色 */

/* 主题列表（用于选择器） */
extern const tui_theme_t * const tui_themes[];
extern const int tui_themes_count;

/* 切换主题；返回当前主题 */
const tui_theme_t *tui_set_theme(const tui_theme_t *t);
const tui_theme_t *tui_theme_current(void);
const tui_theme_t *tui_theme_by_name(const char *name);

/* ══════════════════════════════════════════════════════
 *  布局系统 (layout.c)
 * ══════════════════════════════════════════════════════ */

/* 不透明类型 */
typedef struct tui_layout tui_layout_t;

/* 渲染回调：在给定区域内绘制内容 */
typedef int (*tui_render_fn)(int fd, const tui_rect_t *area, void *userdata);

/* ── Flex 布局枚举（CSS-like） ──────────────────────── */

typedef enum {
    TUI_FLEX_DIR_ROW,        /* 水平排列（默认 HBox） */
    TUI_FLEX_DIR_COLUMN,     /* 垂直排列（默认 VBox） */
} tui_flex_dir_t;

typedef enum {
    TUI_JUSTIFY_START,       /* 起始对齐（默认） */
    TUI_JUSTIFY_CENTER,      /* 居中 */
    TUI_JUSTIFY_END,         /* 末端对齐 */
    TUI_JUSTIFY_BETWEEN,     /* 两端对齐（首尾贴边，间隙均分） */
    TUI_JUSTIFY_AROUND,      /* 环绕（首尾 1/2 gap） */
    TUI_JUSTIFY_EVENLY,      /* 完全均分 */
} tui_justify_t;

typedef enum {
    TUI_ALIGN_START,         /* 起始对齐（默认） */
    TUI_ALIGN_CENTER,        /* 居中 */
    TUI_ALIGN_END,           /* 末端对齐 */
    TUI_ALIGN_STRETCH,       /* 拉伸填满 */
} tui_align_t;

/* 创建容器 */
tui_layout_t *tui_layout_vbox(int spacing);  /* 垂直盒子 */
tui_layout_t *tui_layout_hbox(int spacing);  /* 水平盒子 */
tui_layout_t *tui_layout_leaf(tui_render_fn fn, void *userdata); /* 叶子节点 */
tui_layout_t *tui_layout_leaf_with_free(tui_render_fn fn, void *userdata,
                                         void (*free_fn)(void *));  /* 带自动释放 userdata */

/* 创建 Flex 容器（direction=行/列，gap=子项间距） */
tui_layout_t *tui_layout_flex(tui_flex_dir_t dir, int gap);

/* 设置容器主轴对齐 (justify-content) 与交叉轴对齐 (align-items) */
void tui_layout_justify(tui_layout_t *node, tui_justify_t j);
void tui_layout_align(tui_layout_t *node, tui_align_t a);

/* 添加子节点。
 *   weight/grow: 主轴上的 grow factor（0=按 content/basis 算，>0=按比例瓜分剩余）
 *   basis     : 主轴上的固定尺寸（0=自适应，>0=精确像素）
 *   align     : 单项交叉轴对齐（覆盖容器默认） */
int tui_layout_add(tui_layout_t *parent, tui_layout_t *child, int weight);
int tui_layout_add_flex(tui_layout_t *parent, tui_layout_t *child,
                        double grow, int basis);

/* 设置容器内边距 (上下左右，单位字符) */
void tui_layout_pad(tui_layout_t *node, int top, int right, int bottom, int left);

/* 设置子节点的交叉轴对齐（per-child override） */
void tui_layout_child_align(tui_layout_t *parent, int index, tui_align_t a);

/* 渲染整棵布局树 */
int tui_layout_render(int fd, tui_layout_t *root, tui_rect_t area);

/* 释放整棵布局树 (递归释放所有子节点) */
void tui_layout_free(tui_layout_t *layout);

/* 递归检查布局树的有效性 */
int tui_layout_valid(const tui_layout_t *layout);

/* ══════════════════════════════════════════════════════
 *  工具函数 (layout.c, dialog.c)
 * ══════════════════════════════════════════════════════ */

/* 写入指定数量的空格 */
int tui_spaces(int fd, int n);

/* 绘制水平分隔线 (填充字符) */
int tui_hline(int fd, int col, int width, char ch, tui_color_t color);

/* 带颜色写入文本 */
int tui_cprintf(int fd, tui_color_t fg, tui_color_t bg, const char *fmt, ...);

/* 绘制边框。inner 被修改为内容区域 */
int tui_draw_border(int fd, tui_rect_t *inner, const char *title,
                    int style, tui_color_t color);

/* ── CJK / Unicode 宽度 ──────────────────────────── */

/* 返回 UTF-8 字符串的终端显示宽度：ASCII=1，CJK=2 */
int tui_strwidth(const char *s);

/* 返回前 max 列能容纳的最大字节数（截断用） */
int tui_truncate(const char *s, int max_cols);

/* ══════════════════════════════════════════════════════
 *  显示模式模板 (layout.c)
 * ══════════════════════════════════════════════════════ */

/* 全屏内容，无 header/footer */
tui_layout_t *tui_layout_fullscreen(tui_render_fn fn, void *data);

/* 在屏幕中央创建一个限定宽高的盒子 */
tui_layout_t *tui_layout_centered(int width, int height,
                                  tui_render_fn fn, void *data);

/* header + 居中内容 + footer */
tui_layout_t *tui_layout_wizard(const char *title,
                                tui_render_fn fn, void *data,
                                const char *footer);

/* 左侧导航 + 右侧内容 */
tui_layout_t *tui_layout_dual(int sidebar_width, const char *sidebar_title,
                              tui_render_fn side_fn, void *side_data,
                              tui_render_fn content_fn, void *content_data);

/* ══════════════════════════════════════════════════════
 *  Tab 标签栏
 * ══════════════════════════════════════════════════════ */

typedef struct {
    const char *label;
    const char *badge;      /* 角标文本，可为 NULL */
    int         active;
    int         disabled;
} tui_tab_t;

typedef struct {
    tui_tab_t *tabs;
    int        ntabs;
    int        selected;     /* 当前选中 */
    tui_color_t accent;      /* active 颜色 (默认 theme.accent) */
} tui_tabbar_t;

int  tui_tabbar_render(int fd, const tui_rect_t *area, void *userdata);
tui_layout_t *tui_tabbar_new(tui_tab_t *tabs, int ntabs, int selected);

/* ══════════════════════════════════════════════════════
 *  KeyHint 快捷键提示
 *  现代化底部状态栏：细线 + chip 风格
 * ══════════════════════════════════════════════════════ */

typedef struct {
    const char *key;
    const char *label;
    tui_color_t color;
} tui_keyhint_t;

typedef struct {
    tui_keyhint_t *hints;
    int            nhints;
} tui_keyhints_t;

int  tui_keyhints_render(int fd, const tui_rect_t *area, void *userdata);
tui_layout_t *tui_keyhints_new(tui_keyhint_t *hints, int nhints);

/* ══════════════════════════════════════════════════════
 *  Stat 数据卡
 * ══════════════════════════════════════════════════════ */

typedef enum {
    TUI_TREND_UP    = 1,
    TUI_TREND_DOWN  = -1,
    TUI_TREND_FLAT  = 0,
} tui_trend_t;

typedef struct {
    char        label[32];
    char        value[16];
    tui_color_t fg;
    int         trend;
    char        delta[16];
    tui_color_t trend_color;
} tui_stat_t;

int  tui_stat_render(int fd, const tui_rect_t *area, void *userdata);
tui_layout_t *tui_stat_new(const char *label, const char *value,
                           tui_color_t fg, int trend, const char *delta);

/* ══════════════════════════════════════════════════════
 *  Sparkline 迷你折线
 * ══════════════════════════════════════════════════════ */

typedef struct {
    const int   *data;
    int          npoints;
    int          max_val;
    tui_color_t  fg;
    int          filled;
    tui_color_t  fill_color;
} tui_sparkline_t;

int  tui_sparkline_render(int fd, const tui_rect_t *area, void *userdata);
tui_layout_t *tui_sparkline_new(const int *data, int npoints, tui_color_t fg);

/* ══════════════════════════════════════════════════════
 *  Card 卡片
 * ══════════════════════════════════════════════════════ */

typedef struct {
    char         title[64];
    char         subtitle[64];
    tui_color_t  title_fg;
    tui_color_t  bg;
    tui_render_fn content_fn;
    void        *content_data;
} tui_card_t;

int  tui_card_render(int fd, const tui_rect_t *area, void *userdata);
tui_layout_t *tui_card_new(const char *title, tui_render_fn content_fn, void *data);

/* ══════════════════════════════════════════════════════
 *  Style 预设
 *  Banner / Progress / Spinner / Badge 样式
 * ══════════════════════════════════════════════════════ */

/* Banner 风格 */
typedef enum {
    TUI_BANNER_SIMPLE = 0,    /* 单线，简洁 */
    TUI_BANNER_DOUBLE = 1,    /* 双线，醒目 */
    TUI_BANNER_HEAVY  = 2,    /* 粗框 + 加粗标题 */
    TUI_BANNER_ANGLED = 3,    /* 斜角装饰 `『 』` */
} tui_banner_style_t;

typedef struct {
    char        text[128];
    char        sub[128];
    tui_color_t color;
    int         style;          /* tui_banner_style_t */
    int         gradient;       /* 标题使用渐变色（要求 24-bit） */
    const char *tag;            /* 顶部小标签，如 "v1.0" */
} tui_banner_t;

tui_layout_t *tui_banner(const tui_banner_t *cfg);

/* 公开的渲染函数（可用作 layout 叶子或直接调用） */
int tui_banner_render(int fd, const tui_rect_t *area, void *userdata);

/* Progress 风格 */
typedef enum {
    TUI_PROGRESS_BLOCK     = 0,  /* ████░░░░ (默认) */
    TUI_PROGRESS_SEGMENT   = 1,  /* ▰▰▰▱▱▱ */
    TUI_PROGRESS_DOT       = 2,  /* ●●●○○○ */
    TUI_PROGRESS_PIPE      = 3,  /* ━━━━━ */
    TUI_PROGRESS_SHADE     = 4,  /* ░▒▓█  渐变 */
} tui_progress_style_t;

typedef struct {
    double      value;
    int         bar_width;
    tui_color_t fill_color;
    tui_color_t empty_color;
    char        label[48];
    int         show_percent;
    int         style;          /* tui_progress_style_t */
    tui_color_t label_color;    /* 标签颜色 */
} tui_progress_t;

tui_layout_t *tui_progress(const tui_progress_t *cfg);

/* Spinner 预设帧 */
#define TUI_SPINNER_DOTS   "⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏ "   /* 默认（点状旋转） */
#define TUI_SPINNER_CLOCK  "🕐🕑🕒🕓🕔🕕🕖🕗🕘🕙🕚🕛"
#define TUI_SPINNER_WAVE   "⠁⠂⠄⠂"
#define TUI_SPINNER_BOUNCE "⠁⠃⠇⠧⠷⠿⠿⠷⠧⠇⠃⠁"
#define TUI_SPINNER_ARROW  "←↖↑↗→↘↓↙"
#define TUI_SPINNER_PIPE   "┤┘┴└├┌┬┐"
#define TUI_SPINNER_BAR    "▁▂▃▄▅▆▇█▇▆▅▄▃▂▁"

typedef struct {
    int         frame;
    tui_color_t color;
    const char *frames;          /* 帧序列 */
    char        label[32];       /* 后置文本 */
} tui_spinner_t;

tui_layout_t *tui_spinner(const tui_spinner_t *cfg);

/* Badge 风格 */
typedef enum {
    TUI_BADGE_FILL   = 0,        /* [ TEXT ] 色块背景 */
    TUI_BADGE_PILL   = 1,        /* ● TEXT ●  圆点 */
    TUI_BADGE_OUTLN  = 2,        /* ▸ TEXT ▸ 边框（用方括号） */
    TUI_BADGE_DOT    = 3,        /* ● TEXT    前缀圆点 */
    TUI_BADGE_TAG    = 4,        /* #tag       hash 风格 */
} tui_badge_style_t;

typedef struct {
    char        text[64];
    tui_color_t fg, bg;
    int         style;           /* tui_badge_style_t */
} tui_badge_t;

tui_layout_t *tui_badge(const tui_badge_t *cfg);

/* Table */
#define TUI_TABLE_MAX_COLS  16
#define TUI_TABLE_MAX_ROWS  256

/* 列定义 */
typedef struct {
    char        header[32];
    int         width;         /* 列宽 (0=自适应) */
    int         align;         /* -1=左, 0=中, 1=右 */
} tui_column_t;

/* 表格数据回调：返回第 row 行第 col 列的字符串 */
typedef const char *(*tui_table_cell_fn)(int row, int col, void *userdata);

/* 表格状态 */
typedef struct {
    tui_column_t      columns[TUI_TABLE_MAX_COLS];
    int               ncols;
    int               nrows;
    int               selected;        /* 选中行 (-1=无) */
    tui_table_cell_fn cell_fn;         /* 单元格内容回调 */
    void             *userdata;
    tui_color_t       header_bg;
    tui_color_t       select_bg;
    /* 24-bit 表格背景：设为 (0,0,0) 表示使用主题 card_bg（推荐） */
    tui_rgb_t         bg;
    tui_rgb_t         header_bg_rgb;
    tui_rgb_t         select_bg_rgb;
} tui_table_t;

int  tui_table_render(int fd, const tui_rect_t *area, void *userdata);
int  tui_table_handle(tui_table_t *t, tui_event_t *ev);

/* Table 增强 */
typedef struct {
    int         zebra;           /* 0/1 */
    tui_color_t zebra_bg;        /* 0=主题默认 */
    tui_color_t zebra_fg;        /* 0=默认前景 */
    int         rounded;         /* 0/1 圆角 */
    int         header_align;    /* -1, 0, 1 */
} tui_table_style_t;

tui_table_t *tui_table_new(tui_column_t *cols, int ncols,
                              tui_table_cell_fn cell_fn, int nrows,
                              const tui_table_style_t *style);

/* ══════════════════════════════════════════════════════
 *  Selectable List 可选择列表 (list.c)
 *  支持键盘导航、滚动、选择的列表组件
 *  适合菜单/文件列表/包选择/命令历史
 * ══════════════════════════════════════════════════════ */

typedef struct tui_list tui_list_t;

/* 列表项 */
typedef struct {
    char  label[128];
    char  secondary[64];
    void *userdata;
    int   disabled;
} tui_list_item_t;

/* 创建和销毁 */
tui_list_t *tui_list_new(tui_list_item_t *items, int nitems);
void        tui_list_free(tui_list_t *list);

/* 渲染和事件处理 */
int         tui_list_render(int fd, const tui_rect_t *area, void *userdata);
int         tui_list_handle(tui_list_t *list, tui_event_t *ev);

/* 状态查询 */
int         tui_list_selected(tui_list_t *list);
void       *tui_list_selected_data(tui_list_t *list);

/* 动态修改 */
void        tui_list_select(tui_list_t *list, int idx);
void        tui_list_set_items(tui_list_t *list, tui_list_item_t *items, int nitems);

/* 将列表包装为 layout 叶子节点 */
tui_layout_t *tui_list_layout(tui_list_t *list);

/* ══════════════════════════════════════════════════════
 *  Dialog 对话框 (dialog.c)
 *  消息框/确认框/警告/错误/输入对话框
 *  适合安装器/包管理器/设置工具/ai agent 交互
 * ══════════════════════════════════════════════════════ */

/* 按钮位掩码 */
#define TUI_DLG_OK      0x01
#define TUI_DLG_CANCEL  0x02
#define TUI_DLG_YES     0x04
#define TUI_DLG_NO      0x08
#define TUI_DLG_RETRY   0x10
#define TUI_DLG_ABORT   0x20
#define TUI_DLG_IGNORE  0x40

/* 对话框类型 */
typedef enum {
    TUI_DLG_INFO,
    TUI_DLG_WARNING,
    TUI_DLG_ERROR,
    TUI_DLG_QUESTION,
    TUI_DLG_INPUT,
} tui_dlg_type_t;

/* 对话框结构 */
typedef struct {
    tui_dlg_type_t type;          /* 类型 */
    char            title[64];    /* 标题 */
    char            message[384]; /* 消息正文 */
    int             buttons;      /* 按钮位掩码 */
    int             selected_btn; /* 当前选中按钮索引 */
    char            input[256];   /* 输入框内容 */
    int             input_cursor; /* 输入光标位置 */
    int             input_active; /* 是否在输入状态 */
} tui_dialog_t;

/* 渲染和处理 */
int tui_dialog_render(int fd, const tui_rect_t *area, void *userdata);
int tui_dialog_handle(tui_dialog_t *dlg, tui_event_t *ev);

/* 结果查询 */
int  tui_dialog_result(tui_dialog_t *dlg);       /* 返回按下的按钮 (如 TUI_DLG_OK) */
const char *tui_dialog_input(tui_dialog_t *dlg); /* 返回输入文本 */

/* 包装为 layout 叶子 */
tui_layout_t *tui_dialog_layout(const char *title, const char *message,
                                tui_dlg_type_t type, int buttons);

/* 阻塞式简便对话框（内部跑事件循环，仅用于全屏模式） */
int tui_dialog_blocking(int fd, tui_rect_t area, const char *title,
                        const char *message, tui_dlg_type_t type, int buttons);

/* ══════════════════════════════════════════════════════
 *  Text Input 文本输入框 (input.c)
 *  单行文本输入，支持密码模式
 *  适合登录/搜索/命令输入
 * ══════════════════════════════════════════════════════ */

typedef struct {
    char   buffer[512];
    int    cursor;
    int    echo_char;      /* 0=正常, '*'=密码 */
    char   prompt[64];
    int    active;
} tui_input_t;

int  tui_input_render(int fd, const tui_rect_t *area, void *userdata);
int  tui_input_handle(tui_input_t *in, tui_event_t *ev);
void tui_input_reset(tui_input_t *in);
int  tui_input_done(tui_input_t *in);   /* 是否提交 (回车) */
int  tui_input_canceled(tui_input_t *in); /* 是否取消 (ESC) */

/* 包装为 layout 叶子 */
tui_layout_t *tui_input_layout(tui_input_t *in);

/* ══════════════════════════════════════════════════════
 *  可视化组件 (widgets_viz.c)
 *
 *  Gauge 仪表盘 / Heatmap 热力图 / BarChart 条形图
 *  Marquee 跑马灯 / Tree 文件树 / ActivityLog 活动日志
 *  Pulse 脉动指示器 / Box 装饰盒
 * ══════════════════════════════════════════════════════ */

/* Gauge 风格 */
typedef enum {
    TUI_GAUGE_LINEAR = 0,    /* 线性进度 + 刻度 */
    TUI_GAUGE_RING   = 1,    /* 圆环 */
    TUI_GAUGE_BAR    = 2,    /* 块状条 */
    TUI_GAUGE_BULLET = 3,    /* ●○○○ 块状指示 */
} tui_gauge_style_t;

typedef struct {
    char        label[32];
    double      value;        /* 0.0 - 1.0 */
    tui_rgb_t   color;
    tui_rgb_t   bg;
    int         style;
    int         ticks;        /* 刻度数 (0=无) */
    int         show_value;   /* 是否显示百分比 */
} tui_gauge_t;

int tui_gauge_render(int fd, const tui_rect_t *area, void *userdata);
tui_layout_t *tui_gauge_new(const char *label, double value,
                            tui_rgb_t color, int style);

/* BigNum 大数字（数字时钟 / LCD 字体风格）
 *
 * 经典数字时钟字体：每个数字 3 列 × 5 行 = 15 个像素位，
 * 用全块字符 █ (U+2588) 渲染高对比度像素，空像素用空格。
 * 用于强调大数字（CPU%、内存用量等）。 */
int tui_bignum_render_at(int fd, int row, int col, tui_rgb_t fg, tui_rgb_t bg,
                         const char *num_str);
int tui_bignum_width(const char *num_str);

/* Heatmap 热力图（Git 贡献墙风格） */
typedef struct {
    const double *data;
    int            rows;
    int            cols;
    int            cell_h;     /* 1=单行，2=双行 */
    tui_rgb_t      bg;
    int            show_row_labels;
    const char   **row_labels;
} tui_heatmap_t;

int tui_heatmap_render(int fd, const tui_rect_t *area, void *userdata);
tui_layout_t *tui_heatmap_new(const double *data, int rows, int cols,
                              const char **row_labels);

/* BarChart 条形图（横向/纵向） */
typedef struct {
    const char **labels;
    const double *values;
    int           n;
    int           vertical;
    tui_rgb_t     fg;
    tui_rgb_t     bg;
} tui_barchart_t;

int tui_barchart_render(int fd, const tui_rect_t *area, void *userdata);
tui_layout_t *tui_barchart_new(const char **labels, const double *values,
                               int n, int vertical, tui_rgb_t fg);

/* Marquee 跑马灯（自动循环） */
typedef struct {
    const char *text;
    int         frame;
    tui_rgb_t   fg;
    tui_rgb_t   bg;
    int         speed;
} tui_marquee_t;

int  tui_marquee_render(int fd, const tui_rect_t *area, void *userdata);
void tui_marquee_tick(tui_marquee_t *m);
tui_layout_t *tui_marquee_new(const char *text, tui_rgb_t fg);

/* Tree 文件树 */
typedef struct {
    const char *label;
    int         is_leaf;
    int         depth;
    int         expanded;
    int         selected;
} tui_tree_node_t;

typedef struct {
    tui_tree_node_t *nodes;
    int              n;
    int              selected;
    tui_rgb_t        fg;
    tui_rgb_t        bg;
} tui_tree_t;

int tui_tree_render(int fd, const tui_rect_t *area, void *userdata);
tui_layout_t *tui_tree_new(tui_tree_node_t *nodes, int n, int selected,
                           tui_rgb_t fg);

/* ActivityLog 活动日志流 */
typedef struct {
    const char *time;
    const char *level;
    tui_rgb_t   level_fg;
    const char *message;
} tui_log_entry_t;

typedef struct {
    const tui_log_entry_t *entries;
    int                    n;
    int                    scroll;
    tui_rgb_t              bg;
} tui_log_t;

int tui_activitylog_render(int fd, const tui_rect_t *area, void *userdata);
tui_layout_t *tui_activitylog_new(const tui_log_entry_t *entries, int n);

/* Pulse 脉动指示器（◐◓◑◒ 旋转） */
typedef struct {
    int         frame;
    tui_rgb_t   fg;
    tui_rgb_t   bg;
    const char *label;
} tui_pulse_t;

int  tui_pulse_render(int fd, const tui_rect_t *area, void *userdata);
void tui_pulse_tick(tui_pulse_t *p);
tui_layout_t *tui_pulse_new(tui_rgb_t fg, const char *label);

/* Box 装饰盒（带标题、5 种风格） */
typedef enum {
    TUI_BOX_THIN   = 0,
    TUI_BOX_BOLD   = 1,
    TUI_BOX_DOUBLE = 2,
    TUI_BOX_ROUND  = 3,
    TUI_BOX_ASCII  = 4,
} tui_box_style_t;

typedef struct {
    char            title[64];
    tui_rgb_t       title_fg;
    tui_rgb_t       border;
    tui_rgb_t       bg;
    int             style;
    tui_render_fn   content_fn;
    void           *content_data;
} tui_box_t;

int tui_box_render(int fd, const tui_rect_t *area, void *userdata);
tui_layout_t *tui_box_new(const char *title, int style, tui_render_fn fn,
                          void *data);

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_LIBTUI_H */
