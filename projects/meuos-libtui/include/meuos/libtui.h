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
 *  布局系统 (layout.c)
 * ══════════════════════════════════════════════════════ */

/* 不透明类型 */
typedef struct tui_layout tui_layout_t;

/* 渲染回调：在给定区域内绘制内容 */
typedef int (*tui_render_fn)(int fd, const tui_rect_t *area, void *userdata);

/* 创建容器 */
tui_layout_t *tui_layout_vbox(int spacing);  /* 垂直盒子 */
tui_layout_t *tui_layout_hbox(int spacing);  /* 水平盒子 */
tui_layout_t *tui_layout_leaf(tui_render_fn fn, void *userdata); /* 叶子节点 */

/* 添加子节点。weight：尺寸权重（>=1），0 表示填满剩余空间 */
int tui_layout_add(tui_layout_t *parent, tui_layout_t *child, int weight);

/* 设置容器内边距 (上下左右，单位字符) */
void tui_layout_pad(tui_layout_t *node, int top, int right, int bottom, int left);

/* 渲染整棵布局树 */
int tui_layout_render(int fd, tui_layout_t *root, tui_rect_t area);

/* 释放整棵布局树 (递归释放所有子节点) */
void tui_layout_free(tui_layout_t *layout);

/* 递归检查布局树的有效性 */
int tui_layout_valid(const tui_layout_t *layout);

/* ══════════════════════════════════════════════════════
 *  widget.c — 画板元素
 * ══════════════════════════════════════════════════════ */

/* ── 边框面板 ────────────────────────────────────── */

typedef struct {
    int     border_style;   /* 0: 单线, 1: 双线, 2: 圆角, 3: 粗 */
    tui_color_t border_color;
    tui_color_t title_color;
    char    title[64];      /* 面板标题（可为空） */
    tui_render_fn content_fn;
    void   *content_data;
} tui_panel_t;

/* 创建面板 widget（返回叶子节点，持有 panel 所有权） */
tui_layout_t *tui_panel_new(const char *title, tui_render_fn content_fn, void *data);
tui_layout_t *tui_panel_new_styled(const tui_panel_t *cfg);

/* 更新面板配置（不影响已创建的布局节点）—— 在渲染前调用 */
void tui_panel_set_style(tui_panel_t *panel, const tui_panel_t *cfg);

/* ── 填充矩形 ────────────────────────────────────── */

typedef struct {
    tui_color_t fg, bg;
    tui_attr_t  attr;
} tui_style_t;

/* 在矩形区域内填充背景色 */
int tui_fill_rect(int fd, tui_rect_t area, tui_color_t bg);

/* 绘制带样式的 (可选中文字 + 填充) */
int tui_styled_text(int fd, tui_rect_t area, const char *text, tui_style_t style);

/* 绘制边框。inner 被修改为内容区域 */
int tui_draw_border(int fd, tui_rect_t *inner, const char *title,
                    int style, tui_color_t color);

/* ── 进度条 ──────────────────────────────────────── */

typedef struct {
    double      value;         /* 0.0 ~ 1.0 */
    int         bar_width;     /* 进度条视觉宽度 (0=自适应) */
    tui_color_t fill_color;    /* 已填充颜色 (默认 theme.accent) */
    tui_color_t empty_color;   /* 未填充颜色 (默认 dim) */
    char        label[48];     /* 前置标签 */
    int         show_percent;  /* 是否显示百分比文本 */
} tui_progress_t;

/* 进度条渲染回调（可用作 layout 叶子） */
int  tui_progress_render(int fd, const tui_rect_t *area, void *userdata);

/* ── 旋转器 ──────────────────────────────────────── */

typedef struct {
    int         frame;         /* 当前帧 (0-based) */
    tui_color_t color;
    char        frames[16];   /* 动画帧字符序列 (默认: "⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏ ") */
} tui_spinner_t;

#define TUI_SPINNER_FRAMES "⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏ "

/* 旋转器渲染回调 */
int  tui_spinner_render(int fd, const tui_rect_t *area, void *userdata);

/* 前进一帧 */
void tui_spinner_tick(tui_spinner_t *s);

/* ── 状态栏 ──────────────────────────────────────── */

typedef struct {
    char        left[128];    /* 左对齐文本 */
    char        right[128];   /* 右对齐文本 */
    tui_color_t bg;
    tui_color_t fg;
} tui_statusbar_t;

/* 状态栏渲染回调 */
int  tui_statusbar_render(int fd, const tui_rect_t *area, void *userdata);

/* ── 模板快捷函数 ─────────────────────────────────── */

/* 快速创建一个完整布局模板：header + content + statusbar
 * header: 标题字符串
 * status_left / status_right: 状态栏文本
 * content_fn / data: 内容区渲染回调
 */
tui_layout_t *tui_app_layout(const char *header,
                             tui_render_fn content_fn, void *content_data,
                             const char *status_left, const char *status_right);

/* 创建一个分栏布局：sidebar + content
 * sidebar_width: 侧栏宽度（字符数）
 */
tui_layout_t *tui_split_layout(int sidebar_width,
                               tui_render_fn side_fn, void *side_data,
                               tui_render_fn content_fn, void *content_data);

/* ── 辅助输出 ─────────────────────────────────────── */

/* 写入指定数量的空格 */
int tui_spaces(int fd, int n);

/* 绘制水平分隔线 (填充字符) */
int tui_hline(int fd, int col, int width, char ch, tui_color_t color);

/* 带颜色写入文本 */
int tui_cprintf(int fd, tui_color_t fg, tui_color_t bg, const char *fmt, ...);

/* ══════════════════════════════════════════════════════
 *  显示模式模板 (layout.c)
 *  预定义布局模板，适用不同使用场景
 * ══════════════════════════════════════════════════════ */

/* ── 全屏模式 ────────────────────────────────── */
/* 全屏内容，无 header/footer，适合编辑器/阅读器/调试工具 */
tui_layout_t *tui_layout_fullscreen(tui_render_fn fn, void *data);

/* ── 居中模式 ────────────────────────────────── */
/* 在屏幕中央创建一个限定宽高的盒子，适合弹窗/安装器向导/对话框 */
tui_layout_t *tui_layout_centered(int width, int height,
                                  tui_render_fn fn, void *data);

/* ── 向导模式 ────────────────────────────────── */
/* header + 居中内容 + footer，适合安装器/setup/config 引导 */
tui_layout_t *tui_layout_wizard(const char *title,
                                tui_render_fn fn, void *data,
                                const char *footer);

/* ── 双栏模式 ────────────────────────────────── */
/* 左侧导航 + 右侧内容，适合配置工具/包管理器 */
tui_layout_t *tui_layout_dual(int sidebar_width, const char *sidebar_title,
                              tui_render_fn side_fn, void *side_data,
                              tui_render_fn content_fn, void *content_data);

/* ══════════════════════════════════════════════════════
 *  Label 标签 (widget.c)
 *  带样式的文本/标题
 * ══════════════════════════════════════════════════════ */

typedef struct {
    char        text[256];
    tui_color_t fg, bg;
    tui_attr_t  attr;
    int         align;     /* -1=左, 0=中, 1=右 */
} tui_label_t;

/* 标签渲染回调 */
int  tui_label_render(int fd, const tui_rect_t *area, void *userdata);

/* 便捷标签创建函数 */
tui_layout_t *tui_label_new(const char *text, tui_color_t fg);
tui_layout_t *tui_heading(const char *text, tui_color_t fg);

/* ══════════════════════════════════════════════════════
 *  Separator 分隔线 (widget.c)
 * ══════════════════════════════════════════════════════ */

typedef struct {
    int         vertical;
    char        line_char;
    char        label[64];
    tui_color_t color;
} tui_separator_t;

int  tui_separator_render(int fd, const tui_rect_t *area, void *userdata);

/* 快捷创建：水平分隔线（可选标签） */
tui_layout_t *tui_hr(tui_color_t color);
tui_layout_t *tui_hr_label(const char *label, tui_color_t color);

/* ══════════════════════════════════════════════════════
 *  Badge 徽章 (widget.c)
 *  带色块的小标签，适合状态标记/版本号
 * ══════════════════════════════════════════════════════ */

typedef struct {
    char        text[64];
    tui_color_t fg, bg;
} tui_badge_t;

int  tui_badge_render(int fd, const tui_rect_t *area, void *userdata);
tui_layout_t *tui_badge_new(const char *text, tui_color_t bg);

/* ══════════════════════════════════════════════════════
 *  Table 表格 (widget.c)
 *  带表头的结构化数据展示
 * ══════════════════════════════════════════════════════ */

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
} tui_table_t;

int  tui_table_render(int fd, const tui_rect_t *area, void *userdata);
int  tui_table_handle(tui_table_t *t, tui_event_t *ev);

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

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_LIBTUI_H */
