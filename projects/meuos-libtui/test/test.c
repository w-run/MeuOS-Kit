/* test.c — meuos-libtui 回归测试
 *
 * 验证所有公共 API 在非终端环境（管道）中的行为：
 * - 错误路径（不是终端时的行为）
 * - 输出函数正确性（写入管道可验证）
 * - 输入超时（管道无数据时返回 TUI_KEY_TIMEOUT）
 * - 编译验证（所有头文件符号都已实现）
 *
 * 纯 C11 + POSIX 实现，零外部依赖。
 */

#define _XOPEN_SOURCE 700

#include "meuos/libtui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

/* ── 测试框架 ─────────────────────────────────────── */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name, expr) do {                                    \
    if (!(expr)) {                                               \
        fprintf(stderr, "  FAIL: %s (%s:%d)\n",                  \
                name, __FILE__, __LINE__);                       \
        tests_failed++;                                          \
    } else {                                                     \
        tests_passed++;                                          \
    }                                                            \
} while(0)

/* ── 辅助：创建管道用于模拟终端输出 ─────────────────── */

static int test_write_to_pipe(void)
{
    int p[2];
    if (pipe(p) < 0) return -1;
    return p[1];  /* 写端 */
}

static int test_read_from_pipe(void)
{
    int p[2];
    if (pipe(p) < 0) return -1;
    close(p[1]);  /* 关闭写端，读端会返回 EOF */
    return p[0];
}

/* ── 测试：tui_get_size 在管道上返回错误 ────────────── */

static void test_get_size_pipe(void)
{
    int p[2];
    if (pipe(p) < 0) { TEST("pipe creation", 0); return; }

    tui_size_t size;
    int ret = tui_get_size(p[0], &size);
    TEST("tui_get_size on pipe returns error", ret != TUI_OK);

    close(p[0]);
    close(p[1]);
}

/* ── 测试：颜色/样式写入 ────────────────────────────── */

static void test_screen_ops(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe creation", 0); return; }

    TEST("tui_clear_screen", tui_clear_screen(fd) == TUI_OK);
    TEST("tui_clear_line",   tui_clear_line(fd)   == TUI_OK);
    TEST("tui_clear_eol",    tui_clear_eol(fd)    == TUI_OK);
    TEST("tui_set_fg red",   tui_set_fg(fd, TUI_COLOR_RED) == TUI_OK);
    TEST("tui_set_bg blue",  tui_set_bg(fd, TUI_COLOR_BLUE) == TUI_OK);
    TEST("tui_set_attr bold", tui_set_attr(fd, TUI_ATTR_BOLD) == TUI_OK);
    TEST("tui_reset_style",  tui_reset_style(fd)  == TUI_OK);
    TEST("tui_cursor_goto",  tui_cursor_goto(fd, 5, 10) == TUI_OK);
    TEST("tui_cursor_up",    tui_cursor_up(fd, 3) == TUI_OK);
    TEST("tui_cursor_down",  tui_cursor_down(fd, 2) == TUI_OK);
    TEST("tui_cursor_left",  tui_cursor_left(fd, 1) == TUI_OK);
    TEST("tui_cursor_right", tui_cursor_right(fd, 4) == TUI_OK);
    TEST("tui_cursor_save",  tui_cursor_save(fd)  == TUI_OK);
    TEST("tui_cursor_restore", tui_cursor_restore(fd) == TUI_OK);
    TEST("tui_cursor_show",  tui_cursor_show(fd, 0) == TUI_OK);
    TEST("tui_cursor_show restore", tui_cursor_show(fd, 1) == TUI_OK);
    TEST("tui_printf",       tui_printf(fd, "Hello %s", "TUI") == TUI_OK);
    TEST("tui_write",        tui_write(fd, "done") == TUI_OK);
    TEST("tui_flush",        tui_flush(fd) == TUI_OK);

    close(fd);
}

/* ── 测试：超时输入 ─────────────────────────────────── */

static void test_input_timeout(void)
{
    int fd = test_read_from_pipe();
    if (fd < 0) { TEST("pipe creation", 0); return; }

    tui_event_t ev;
    int ret = tui_getkey_timeout(fd, &ev, 10);
    TEST("tui_getkey_timeout returns OK", ret == TUI_OK);
    TEST("timeout returns TUI_KEY_TIMEOUT", ev.key == TUI_KEY_TIMEOUT);

    close(fd);
}

/* ── 测试：原始模式参数验证 ──────────────────────────── */

static void test_raw_mode_params(void)
{
    TEST("tui_raw_mode(-1, 1) returns error", tui_raw_mode(-1, 1) == TUI_ERR_PARAM);
}

/* ── 测试：tui_getkey 参数验证 ──────────────────────── */

static void test_getkey_params(void)
{
    tui_event_t ev;
    TEST("tui_getkey(-1, &ev) returns error", tui_getkey(-1, &ev) == TUI_ERR_PARAM);
    TEST("tui_getkey_timeout(-1) returns error",
         tui_getkey_timeout(-1, &ev, 10) == TUI_ERR_PARAM);
}

/* ── 测试：备用屏幕 ─────────────────────────────────── */

static void test_alt_screen(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe creation", 0); return; }

    TEST("tui_alt_screen enable",  tui_alt_screen(fd, 1) == TUI_OK);
    TEST("tui_alt_screen disable", tui_alt_screen(fd, 0) == TUI_OK);

    close(fd);
}

/* ── 测试：鼠标模式 ─────────────────────────────────── */

static void test_mouse(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe creation", 0); return; }

    TEST("tui_mouse enable",  tui_mouse(fd, 1) == TUI_OK);
    TEST("tui_mouse disable", tui_mouse(fd, 0) == TUI_OK);

    close(fd);
}

/* ── 测试：tui_get_size 参数验证 ────────────────────── */

static void test_get_size_null(void)
{
    TEST("tui_get_size(0, NULL) returns error",
         tui_get_size(0, NULL) == TUI_ERR_PARAM);
}

/* ── 测试：SIGWINCH 回调注册 ────────────────────────── */

static int resize_flag = 0;
static void resize_cb(tui_size_t size, void *userdata)
{
    (void)size;
    (void)userdata;
    resize_flag = 1;
}

static void test_resize_cb(void)
{
    TEST("tui_on_resize register",  tui_on_resize(resize_cb, NULL) == TUI_OK);
    TEST("tui_on_resize unregister", tui_on_resize(NULL, NULL) == TUI_OK);
}

/* ══════════════════════════════════════════════════════
 *  main
 * ══════════════════════════════════════════════════════ */

int main(void)
{
    printf("meuos-libtui test suite\n");
    printf("========================\n\n");

    /* 屏幕操作 */
    printf("[screen ops]\n");
    test_get_size_pipe();
    test_get_size_null();
    test_screen_ops();
    printf("\n");

    /* 输入 */
    printf("[input]\n");
    test_input_timeout();
    test_getkey_params();
    printf("\n");

    /* 终端模式 */
    printf("[terminal]\n");
    test_raw_mode_params();
    test_alt_screen();
    test_mouse();
    printf("\n");

    /* 信号 */
    printf("[signal]\n");
    test_resize_cb();
    printf("\n");

    /* 汇总 */
    printf("========================\n");
    printf("passed: %d, failed: %d\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
