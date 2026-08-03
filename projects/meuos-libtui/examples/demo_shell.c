/* demo_shell.c — AI Agent 风格的 TUI 终端原型
 *
 * 布局：顶部标题栏 + 中间滚动输出区 + 底部输入框 + 状态栏
 * 功能：模拟 Shell 交互，支持命令输入、输出展示、历史滚动
 *
 * 运行: make demo_shell && ./build/demo_shell
 *
 * 按键:
 *   Enter  - 执行输入的命令
 *   ↑/↓    - 浏览历史输出
 *   Ctrl-L - 清屏
 *   Ctrl-C - 清空当前输入
 *   ESC/q  - 退出
 */

#define _XOPEN_SOURCE 700

#include "meuos/libtui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

/* ══════════════════════════════════════════════════════
 *  滚动缓冲区
 * ══════════════════════════════════════════════════════ */

#define MAX_LINES    256
#define LINE_WIDTH   512

typedef struct {
    char    text[LINE_WIDTH];
    int     type;       /* 0=normal, 1=command, 2=output, 3=error, 4=info */
} log_line_t;

typedef struct {
    log_line_t  lines[MAX_LINES];
    int         count;      /* 总行数 */
    int         scroll;     /* 滚动偏移（从底部算） */
} scrollbuf_t;

static void sb_init(scrollbuf_t *sb)
{
    memset(sb, 0, sizeof(*sb));
}

static void sb_append(scrollbuf_t *sb, const char *text, int type)
{
    if (sb->count >= MAX_LINES) {
        /* 滚动：移除最旧的行 */
        memmove(&sb->lines[0], &sb->lines[1],
                (size_t)(MAX_LINES - 1) * sizeof(log_line_t));
        sb->count = MAX_LINES - 1;
    }
    strncpy(sb->lines[sb->count].text, text, LINE_WIDTH - 1);
    sb->lines[sb->count].text[LINE_WIDTH - 1] = '\0';
    sb->lines[sb->count].type = type;
    sb->count++;
    /* 自动滚到底部 */
    sb->scroll = 0;
}

/* ══════════════════════════════════════════════════════
 *  Shell 模拟器状态
 * ══════════════════════════════════════════════════════ */

typedef struct {
    scrollbuf_t    buf;
    tui_input_t    input;
    char           cwd[256];
    int            running;
    int            cmd_count;
} shell_state_t;

/* ── 模拟命令执行 ─────────────────────────────────── */

static void fake_execute(shell_state_t *st, const char *cmd)
{
    char prompt_line[LINE_WIDTH];

    /* 记录命令行 */
    snprintf(prompt_line, sizeof(prompt_line), "$ %s", cmd);
    sb_append(&st->buf, prompt_line, 1);

    st->cmd_count++;

    /* 空命令 */
    if (cmd[0] == '\0') return;

    /* help */
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        sb_append(&st->buf, "Available commands:", 4);
        sb_append(&st->buf, "  help       Show this help", 2);
        sb_append(&st->buf, "  echo TEXT  Print text", 2);
        sb_append(&st->buf, "  date       Show current date/time", 2);
        sb_append(&st->buf, "  ls         List files (simulated)", 2);
        sb_append(&st->buf, "  whoami     Show current user", 2);
        sb_append(&st->buf, "  clear      Clear screen", 2);
        sb_append(&st->buf, "  exit       Exit shell", 2);
        return;
    }

    /* echo */
    if (strncmp(cmd, "echo ", 5) == 0 || strncmp(cmd, "echo\t", 5) == 0) {
        sb_append(&st->buf, cmd + 5, 2);
        return;
    }
    if (strcmp(cmd, "echo") == 0) {
        sb_append(&st->buf, "", 2);
        return;
    }

    /* date */
    if (strcmp(cmd, "date") == 0) {
        time_t t = time(NULL);
        struct tm *tm = localtime(&t);
        char datestr[64];
        strftime(datestr, sizeof(datestr), "%Y-%m-%d %H:%M:%S %Z", tm);
        sb_append(&st->buf, datestr, 2);
        return;
    }

    /* ls (simulated) */
    if (strcmp(cmd, "ls") == 0 || strncmp(cmd, "ls ", 3) == 0) {
        sb_append(&st->buf, "src/       libtui.h    Makefile", 2);
        sb_append(&st->buf, "examples/  libtui.a    ARCHITECTURE.md", 2);
        sb_append(&st->buf, "scripts/   test/       README.md", 2);
        return;
    }

    /* whoami */
    if (strcmp(cmd, "whoami") == 0) {
        sb_append(&st->buf, "meuos", 2);
        return;
    }

    /* clear */
    if (strcmp(cmd, "clear") == 0) {
        st->buf.count = 0;
        st->buf.scroll = 0;
        return;
    }

    /* exit / quit */
    if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
        sb_append(&st->buf, "Bye!", 4);
        st->running = 0;
        return;
    }

    /* unknown */
    snprintf(prompt_line, sizeof(prompt_line),
             "msh: command not found: %s", cmd);
    sb_append(&st->buf, prompt_line, 3);
}

/* ══════════════════════════════════════════════════════
 *  渲染函数
 * ══════════════════════════════════════════════════════ */

/* ── 标题栏 ── */
static int header_render(int fd, const tui_rect_t *area, void *udata)
{
    (void)udata;
    tui_cursor_goto(fd, area->row, area->col);
    tui_set_bg(fd, tui_meuos_theme.accent);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_spaces(fd, area->cols - 1);

    /* 标题文本 */
    tui_cursor_goto(fd, area->row, area->col + 2);
    tui_set_fg(fd, TUI_COLOR_WHITE);
    tui_set_bg(fd, tui_meuos_theme.accent);
    tui_write(fd, "msh — MeuOS Shell (TUI Mode)");

    /* 右侧标识 */
    const char *tag = " AI Agent Terminal ";
    int tag_w = tui_strwidth(tag);
    tui_cursor_goto(fd, area->row, area->col + area->cols - tag_w - 1);
    tui_set_fg(fd, TUI_COLOR_YELLOW);
    tui_set_bg(fd, tui_meuos_theme.accent);
    tui_write(fd, tag);

    tui_reset_style(fd);
    return TUI_OK;
}

/* ── 输出区域（滚动缓冲区）── */
static int output_render(int fd, const tui_rect_t *area, void *udata)
{
    shell_state_t *st = (shell_state_t *)udata;

    /* 边框 */
    tui_rect_t inner = *area;
    tui_draw_border(fd, &inner, "  Output  ", 0, tui_meuos_theme.border);

    if (!tui_rect_valid(&inner)) return TUI_OK;

    int max_lines = inner.rows;
    int total = st->buf.count;

    /* 计算起始行 */
    int start;
    if (st->buf.scroll > 0)
        start = total - max_lines - st->buf.scroll;
    else
        start = total - max_lines;
    if (start < 0) start = 0;

    int visible = total - start;
    if (visible > max_lines) visible = max_lines;

    int y = inner.row;
    int x = inner.col;

    /* 显示可见行 */
    for (int i = 0; i < visible; i++) {
        int idx = start + i;
        if (idx >= total) break;

        log_line_t *ll = &st->buf.lines[idx];
        tui_cursor_goto(fd, y + i, x);

        switch (ll->type) {
        case 1:  /* command */
            tui_set_fg(fd, TUI_COLOR_GREEN);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            break;
        case 2:  /* output */
            tui_set_fg(fd, TUI_COLOR_DEFAULT);
            break;
        case 3:  /* error */
            tui_set_fg(fd, TUI_COLOR_RED);
            break;
        case 4:  /* info */
            tui_set_fg(fd, TUI_COLOR_CYAN);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            break;
        default:
            tui_set_fg(fd, TUI_COLOR_DEFAULT);
            break;
        }

        int bytes = tui_truncate(ll->text, inner.cols);
        write(fd, ll->text, (size_t)bytes);
        tui_reset_style(fd);
    }

    /* 剩余行清空 */
    for (int i = visible; i < max_lines; i++) {
        tui_cursor_goto(fd, y + i, x);
        tui_clear_eol(fd);
    }

    /* 滚动指示器 */
    if (st->buf.scroll > 0) {
        tui_cursor_goto(fd, inner.row + inner.rows - 1, inner.col + inner.cols - 12);
        tui_set_fg(fd, TUI_COLOR_YELLOW);
        tui_set_attr(fd, TUI_ATTR_DIM);
        char scroll_info[16];
        snprintf(scroll_info, sizeof(scroll_info), " ↑%d ", st->buf.scroll);
        tui_write(fd, scroll_info);
        tui_reset_style(fd);
    }

    return TUI_OK;
}

/* ── 输入框区域 ── */
static int input_render(int fd, const tui_rect_t *area, void *udata)
{
    shell_state_t *st = (shell_state_t *)udata;

    /* 边框 */
    tui_rect_t inner = *area;
    tui_color_t input_color = TUI_COLOR_CYAN;
    tui_draw_border(fd, &inner, "  Input  ", 0, input_color);

    if (!tui_rect_valid(&inner)) return TUI_OK;

    /* 提示符 */
    tui_cursor_goto(fd, inner.row, inner.col + 1);
    tui_set_fg(fd, TUI_COLOR_GREEN);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_write(fd, "$ ");
    tui_reset_style(fd);

    /* 输入文本 */
    int prompt_w = 2;
    int text_x = inner.col + 1 + prompt_w;
    int text_w = inner.cols - prompt_w - 2;

    tui_cursor_goto(fd, inner.row, text_x);
    tui_set_fg(fd, TUI_COLOR_WHITE);

    int input_len = (int)strlen(st->input.buffer);
    int show_bytes = tui_truncate(st->input.buffer, text_w);
    write(fd, st->input.buffer, (size_t)show_bytes);

    /* 光标 */
    tui_cursor_goto(fd, inner.row, text_x + input_len);
    tui_set_attr(fd, TUI_ATTR_REVERSE);
    tui_write(fd, " ");
    tui_reset_style(fd);

    /* 第二行：提示信息 */
    if (inner.rows > 1) {
        tui_cursor_goto(fd, inner.row + 1, inner.col + 1);
        tui_set_fg(fd, tui_meuos_theme.dim);
        tui_set_attr(fd, TUI_ATTR_DIM);
        tui_write(fd, "Enter=execute  ↑↓=scroll  Ctrl-L=clear  ESC=exit");
        tui_reset_style(fd);
    }

    return TUI_OK;
}

/* ── 状态栏 ── */
static int statusbar_render(int fd, const tui_rect_t *area, void *udata)
{
    shell_state_t *st = (shell_state_t *)udata;

    tui_cursor_goto(fd, area->row, area->col);
    tui_set_bg(fd, tui_meuos_theme.accent);
    tui_set_fg(fd, TUI_COLOR_WHITE);
    tui_set_attr(fd, TUI_ATTR_BOLD);

    /* 左侧：工作目录 + 命令计数 */
    char left[128];
    snprintf(left, sizeof(left), " %s | cmds: %d ", st->cwd, st->cmd_count);
    tui_write(fd, left);

    /* 中间填充 */
    int left_w = tui_strwidth(left);
    const char *right = " msh v0.1 | TUI mode ";
    int right_w = tui_strwidth(right);
    int pad = area->cols - 1 - left_w - right_w;
    if (pad > 0) tui_spaces(fd, pad);

    /* 右侧 */
    tui_write(fd, right);

    tui_reset_style(fd);
    tui_spaces(fd, 1);
    tui_reset_style(fd);
    return TUI_OK;
}

/* ══════════════════════════════════════════════════════
 *  主循环
 * ══════════════════════════════════════════════════════ */

int main(void)
{
    /* 初始化 */
    tui_raw_mode(0, 1);
    tui_alt_screen(0, 1);
    tui_clear_screen(0);
    tui_cursor_show(0, 0);

    tui_size_t scr;
    if (tui_get_size(0, &scr) != TUI_OK) {
        scr.rows = 30;
        scr.cols = 80;
    }

    shell_state_t st;
    memset(&st, 0, sizeof(st));
    sb_init(&st.buf);
    strcpy(st.cwd, "~");
    st.running = 1;
    st.cmd_count = 0;
    st.input.active = 1;
    strcpy(st.input.prompt, "$ ");

    /* 欢迎信息 */
    sb_append(&st.buf, "MeuOS Shell — TUI Mode (AI Agent Style)", 4);
    sb_append(&st.buf, "Type 'help' for available commands.", 2);
    sb_append(&st.buf, "", 0);

    /* 事件循环 */
    tui_event_t ev;

    while (st.running) {
        /* 计算布局区域 */
        int header_h  = 1;
        int status_h  = 1;
        int input_h   = 4;
        int output_h  = scr.rows - header_h - input_h - status_h;
        if (output_h < 3) output_h = 3;

        int y = 1;
        tui_rect_t header_area  = { y, 1, header_h, scr.cols - 1 };
        y += header_h;

        tui_rect_t output_area  = { y, 1, output_h, scr.cols - 1 };
        y += output_h;

        tui_rect_t input_area   = { y, 1, input_h, scr.cols - 1 };
        y += input_h;

        tui_rect_t status_area  = { y, 1, status_h, scr.cols - 1 };

        /* 渲染 */
        header_render(0, &header_area, &st);
        output_render(0, &output_area, &st);
        input_render(0, &input_area, &st);
        statusbar_render(0, &status_area, &st);

        /* 输入处理 */
        if (tui_getkey(0, &ev) == TUI_OK) {
            switch (ev.key) {
            case TUI_KEY_CR:
            case TUI_KEY_LF:
                /* 执行命令 */
                fake_execute(&st, st.input.buffer);
                tui_input_reset(&st.input);
                st.input.active = 1;
                break;

            case TUI_KEY_ESC:
                st.running = 0;
                break;

            case TUI_KEY_UP:
                if (st.buf.scroll < st.buf.count - 1)
                    st.buf.scroll++;
                break;

            case TUI_KEY_DOWN:
                if (st.buf.scroll > 0)
                    st.buf.scroll--;
                break;

            case TUI_KEY_CTRL_L:
                /* 清屏 */
                st.buf.count = 0;
                st.buf.scroll = 0;
                break;

            case TUI_KEY_CTRL_C:
                /* 清空输入 */
                tui_input_reset(&st.input);
                st.input.active = 1;
                break;

            case TUI_KEY_BS:
            case TUI_KEY_DEL:
                if (st.input.cursor > 0) {
                    int len = (int)strlen(st.input.buffer);
                    st.input.cursor--;
                    memmove(st.input.buffer + st.input.cursor,
                            st.input.buffer + st.input.cursor + 1,
                            (size_t)(len - st.input.cursor));
                    st.input.buffer[len - 1] = '\0';
                }
                break;

            default:
                if (ev.key >= 0x20 && ev.key <= 0x7E) {
                    int len = (int)strlen(st.input.buffer);
                    if (len < (int)sizeof(st.input.buffer) - 1) {
                        st.input.buffer[len] = (char)ev.key;
                        st.input.buffer[len + 1] = '\0';
                        st.input.cursor++;
                    }
                }
                break;
            }
        }
    }

    /* 清理 */
    tui_cursor_show(0, 1);
    tui_clear_screen(0);
    tui_alt_screen(0, 0);
    tui_raw_mode(0, 0);

    return 0;
}
