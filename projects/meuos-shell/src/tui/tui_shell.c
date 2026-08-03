/* tui_shell.c — msh TUI 终端模式（AI Agent 风格）
 *
 * 布局：标题栏 + 滚动输出区 + 输入框 + 状态栏
 * 命令通过 fork+pipe 调用 msh_run_string() 执行，捕获 stdout/stderr。
 *
 * 编译: 需要 libtui.a（Makefile 自动链接）
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "meuos/libtui.h"
#include "msh/exec.h"
#include "msh/msh.h"
#include "msh/tui.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <errno.h>

/* ══════════════════════════════════════════════════════
 *  滚动缓冲区
 * ══════════════════════════════════════════════════════ */

#define MAX_LINES    512
#define LINE_WIDTH   512

typedef struct {
    char    text[LINE_WIDTH];
    int     type;       /* 0=normal, 1=command, 2=output, 3=error, 4=info */
} log_line_t;

typedef struct {
    log_line_t  lines[MAX_LINES];
    int         count;
    int         scroll;
} scrollbuf_t;

static void sb_init(scrollbuf_t *sb) { memset(sb, 0, sizeof(*sb)); }

static void sb_append(scrollbuf_t *sb, const char *text, int type)
{
    if (sb->count >= MAX_LINES) {
        memmove(&sb->lines[0], &sb->lines[1],
                (size_t)(MAX_LINES - 1) * sizeof(log_line_t));
        sb->count = MAX_LINES - 1;
    }
    size_t len = strlen(text);
    if (len >= LINE_WIDTH) len = LINE_WIDTH - 1;
    memcpy(sb->lines[sb->count].text, text, len);
    sb->lines[sb->count].text[len] = '\0';
    sb->lines[sb->count].type = type;
    sb->count++;
    sb->scroll = 0;
}

/* ══════════════════════════════════════════════════════
 *  命令执行（fork + pipe 捕获输出）
 * ══════════════════════════════════════════════════════ */

static void execute_command(scrollbuf_t *buf, const char *cmd)
{
    char prompt_line[LINE_WIDTH];
    snprintf(prompt_line, sizeof(prompt_line), "$ %s", cmd);
    sb_append(buf, prompt_line, 1);

    if (cmd[0] == '\0') return;

    /* 特殊命令：exit/quit */
    if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
        sb_append(buf, "Bye!", 4);
        return;
    }

    /* 特殊命令：clear */
    if (strcmp(cmd, "clear") == 0) {
        buf->count = 0;
        buf->scroll = 0;
        return;
    }

    /* 创建管道捕获子进程输出 */
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        sb_append(buf, "msh: pipe failed", 3);
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        sb_append(buf, "msh: fork failed", 3);
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }

    if (pid == 0) {
        /* 子进程：重定向 stdout/stderr 到管道 */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        /* 调用 msh 执行命令 */
        msh_run_string(cmd, strlen(cmd));
        _exit(msh_last_status);
    }

    /* 父进程：读取子进程输出 */
    close(pipefd[1]);

    char outbuf[8192];
    int total = 0;
    int n;
    while (total < (int)sizeof(outbuf) - 1) {
        n = (int)read(pipefd[0], outbuf + total, sizeof(outbuf) - 1 - total);
        if (n <= 0) break;
        total += n;
    }
    outbuf[total] = '\0';
    close(pipefd[0]);

    /* 等待子进程 */
    int status;
    waitpid(pid, &status, 0);

    /* 按行添加到缓冲区 */
    char *line = outbuf;
    char *nl;
    while ((nl = strchr(line, '\n')) != NULL) {
        *nl = '\0';
        if (*line) sb_append(buf, line, 2);
        line = nl + 1;
    }
    if (*line) sb_append(buf, line, 2);

    /* 如果有非零退出码，显示 */
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        char errline[64];
        snprintf(errline, sizeof(errline), "[exit: %d]", WEXITSTATUS(status));
        sb_append(buf, errline, 3);
    }
}

/* ══════════════════════════════════════════════════════
 *  渲染函数
 * ══════════════════════════════════════════════════════ */

typedef struct {
    scrollbuf_t    buf;
    char           input_buf[512];
    int            input_cursor;
    int            cmd_count;
    int            running;
    char           cwd[256];
} tui_shell_t;

static int header_render(int fd, const tui_rect_t *area, void *udata)
{
    (void)udata;
    tui_cursor_goto(fd, area->row, area->col);
    tui_set_bg(fd, tui_meuos_theme.accent);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_spaces(fd, area->cols - 1);

    tui_cursor_goto(fd, area->row, area->col + 2);
    tui_set_fg(fd, TUI_COLOR_WHITE);
    tui_set_bg(fd, tui_meuos_theme.accent);
    tui_write(fd, "msh — MeuOS Shell");

    const char *tag = " TUI Mode ";
    int tag_w = tui_strwidth(tag);
    tui_cursor_goto(fd, area->row, area->col + area->cols - tag_w - 1);
    tui_set_fg(fd, TUI_COLOR_YELLOW);
    tui_set_bg(fd, tui_meuos_theme.accent);
    tui_write(fd, tag);

    tui_reset_style(fd);
    return TUI_OK;
}

static int output_render(int fd, const tui_rect_t *area, void *udata)
{
    tui_shell_t *st = (tui_shell_t *)udata;
    tui_rect_t inner = *area;
    tui_draw_border(fd, &inner, "  Output  ", 0, tui_meuos_theme.border);

    if (!tui_rect_valid(&inner)) return TUI_OK;

    int max_lines = inner.rows;
    int total = st->buf.count;
    int start = total - max_lines;
    if (st->buf.scroll > 0) start = total - max_lines - st->buf.scroll;
    if (start < 0) start = 0;
    int visible = total - start;
    if (visible > max_lines) visible = max_lines;

    for (int i = 0; i < visible; i++) {
        int idx = start + i;
        if (idx >= total) break;
        log_line_t *ll = &st->buf.lines[idx];
        tui_cursor_goto(fd, inner.row + i, inner.col);

        switch (ll->type) {
        case 1: tui_set_fg(fd, TUI_COLOR_GREEN); tui_set_attr(fd, TUI_ATTR_BOLD); break;
        case 2: tui_set_fg(fd, TUI_COLOR_DEFAULT); break;
        case 3: tui_set_fg(fd, TUI_COLOR_RED); break;
        case 4: tui_set_fg(fd, TUI_COLOR_CYAN); tui_set_attr(fd, TUI_ATTR_BOLD); break;
        default: tui_set_fg(fd, TUI_COLOR_DEFAULT); break;
        }
        int bytes = tui_truncate(ll->text, inner.cols);
        write(fd, ll->text, (size_t)bytes);
        tui_reset_style(fd);
    }

    for (int i = visible; i < max_lines; i++) {
        tui_cursor_goto(fd, inner.row + i, inner.col);
        tui_clear_eol(fd);
    }

    if (st->buf.scroll > 0) {
        tui_cursor_goto(fd, inner.row + inner.rows - 1, inner.col + inner.cols - 12);
        tui_set_fg(fd, TUI_COLOR_YELLOW);
        tui_set_attr(fd, TUI_ATTR_DIM);
        char si[16];
        snprintf(si, sizeof(si), " ^%d ", st->buf.scroll);
        tui_write(fd, si);
        tui_reset_style(fd);
    }
    return TUI_OK;
}

static int input_render(int fd, const tui_rect_t *area, void *udata)
{
    tui_shell_t *st = (tui_shell_t *)udata;
    tui_rect_t inner = *area;
    tui_draw_border(fd, &inner, "  Input  ", 0, TUI_COLOR_CYAN);
    if (!tui_rect_valid(&inner)) return TUI_OK;

    tui_cursor_goto(fd, inner.row, inner.col + 1);
    tui_set_fg(fd, TUI_COLOR_GREEN);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_write(fd, "$ ");
    tui_reset_style(fd);

    int text_x = inner.col + 3;
    int text_w = inner.cols - 4;
    tui_cursor_goto(fd, inner.row, text_x);
    tui_set_fg(fd, TUI_COLOR_WHITE);
    int bytes = tui_truncate(st->input_buf, text_w);
    write(fd, st->input_buf, (size_t)bytes);

    /* 光标 */
    tui_cursor_goto(fd, inner.row, text_x + st->input_cursor);
    tui_set_attr(fd, TUI_ATTR_REVERSE);
    tui_write(fd, " ");
    tui_reset_style(fd);

    if (inner.rows > 1) {
        tui_cursor_goto(fd, inner.row + 1, inner.col + 1);
        tui_set_fg(fd, tui_meuos_theme.dim);
        tui_set_attr(fd, TUI_ATTR_DIM);
        tui_write(fd, "Enter=run  Up/Down=scroll  Ctrl-L=clear  Ctrl-C=cancel  ESC=exit");
        tui_reset_style(fd);
    }
    return TUI_OK;
}

static int statusbar_render(int fd, const tui_rect_t *area, void *udata)
{
    tui_shell_t *st = (tui_shell_t *)udata;
    tui_cursor_goto(fd, area->row, area->col);
    tui_set_bg(fd, tui_meuos_theme.accent);
    tui_set_fg(fd, TUI_COLOR_WHITE);
    tui_set_attr(fd, TUI_ATTR_BOLD);

    char left[128];
    snprintf(left, sizeof(left), " %s | cmds: %d ", st->cwd, st->cmd_count);
    tui_write(fd, left);

    int left_w = tui_strwidth(left);
    const char *right = " msh TUI mode ";
    int right_w = tui_strwidth(right);
    int pad = area->cols - 1 - left_w - right_w;
    if (pad > 0) tui_spaces(fd, pad);
    tui_write(fd, right);

    tui_reset_style(fd);
    tui_spaces(fd, 1);
    tui_reset_style(fd);
    return TUI_OK;
}

/* ══════════════════════════════════════════════════════
 *  入口
 * ══════════════════════════════════════════════════════ */

int msh_tui_main(void)
{
    tui_raw_mode(0, 1);
    tui_alt_screen(0, 1);
    tui_clear_screen(0);
    tui_cursor_show(0, 0);

    tui_size_t scr;
    if (tui_get_size(0, &scr) != TUI_OK) {
        scr.rows = 30; scr.cols = 80;
    }

    tui_shell_t st;
    memset(&st, 0, sizeof(st));
    sb_init(&st.buf);
    strcpy(st.cwd, "~");
    st.running = 1;

    /* 欢迎信息 */
    char welcome[256];
    snprintf(welcome, sizeof(welcome), "MeuOS Shell %s — TUI Mode", msh_version);
    sb_append(&st.buf, welcome, 4);
    sb_append(&st.buf, "Type 'help' for commands. Type 'exit' to quit.", 2);
    sb_append(&st.buf, "", 0);

    /* 安装 SIGCHLD handler */
    extern void msh_job_sigchld_handler(int);
    signal(SIGCHLD, msh_job_sigchld_handler);

    tui_event_t ev;
    while (st.running) {
        int header_h = 1, status_h = 1, input_h = 4;
        int output_h = scr.rows - header_h - input_h - status_h;
        if (output_h < 3) output_h = 3;

        int y = 1;
        tui_rect_t ha = { y, 1, header_h, scr.cols - 1 }; y += header_h;
        tui_rect_t oa = { y, 1, output_h, scr.cols - 1 }; y += output_h;
        tui_rect_t ia = { y, 1, input_h, scr.cols - 1 }; y += input_h;
        tui_rect_t sa = { y, 1, status_h, scr.cols - 1 };

        header_render(0, &ha, &st);
        output_render(0, &oa, &st);
        input_render(0, &ia, &st);
        statusbar_render(0, &sa, &st);

        if (tui_getkey(0, &ev) == TUI_OK) {
            switch (ev.key) {
            case TUI_KEY_CR:
            case TUI_KEY_LF:
                st.cmd_count++;
                execute_command(&st.buf, st.input_buf);
                if (strcmp(st.input_buf, "exit") == 0 || strcmp(st.input_buf, "quit") == 0)
                    st.running = 0;
                st.input_buf[0] = '\0';
                st.input_cursor = 0;
                break;

            case TUI_KEY_ESC:
                st.running = 0;
                break;

            case TUI_KEY_UP:
                if (st.buf.scroll < st.buf.count - 1) st.buf.scroll++;
                break;

            case TUI_KEY_DOWN:
                if (st.buf.scroll > 0) st.buf.scroll--;
                break;

            case TUI_KEY_CTRL_L:
                st.buf.count = 0; st.buf.scroll = 0;
                break;

            case TUI_KEY_CTRL_C:
                st.input_buf[0] = '\0';
                st.input_cursor = 0;
                break;

            case TUI_KEY_BS:
            case TUI_KEY_DEL:
                if (st.input_cursor > 0) {
                    int len = (int)strlen(st.input_buf);
                    st.input_cursor--;
                    memmove(st.input_buf + st.input_cursor,
                            st.input_buf + st.input_cursor + 1,
                            (size_t)(len - st.input_cursor));
                    st.input_buf[len - 1] = '\0';
                }
                break;

            default:
                if (ev.key >= 0x20 && ev.key <= 0x7E) {
                    int len = (int)strlen(st.input_buf);
                    if (len < (int)sizeof(st.input_buf) - 1) {
                        st.input_buf[len] = (char)ev.key;
                        st.input_buf[len + 1] = '\0';
                        st.input_cursor++;
                    }
                }
                break;
            }
        }
    }

    tui_cursor_show(0, 1);
    tui_clear_screen(0);
    tui_alt_screen(0, 0);
    tui_raw_mode(0, 0);
    return 0;
}
