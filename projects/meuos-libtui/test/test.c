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
 *  布局系统测试
 * ══════════════════════════════════════════════════════ */

static void test_rect_valid(void)
{
    tui_rect_t r1 = { 1, 1, 10, 20 };
    TEST("tui_rect_valid: valid rect",   tui_rect_valid(&r1));

    r1.rows = 0;
    TEST("tui_rect_valid: zero rows",   !tui_rect_valid(&r1));

    r1.rows = 10; r1.cols = 0;
    TEST("tui_rect_valid: zero cols",   !tui_rect_valid(&r1));

    TEST("tui_rect_valid: NULL",        !tui_rect_valid(NULL));
}

static void test_layout_create_free(void)
{
    tui_layout_t *vbox = tui_layout_vbox(1);
    TEST("tui_layout_vbox creates",     vbox != NULL);

    tui_layout_t *hbox = tui_layout_hbox(0);
    TEST("tui_layout_hbox creates",     hbox != NULL);

    tui_layout_t *leaf = tui_layout_leaf(NULL, NULL);
    TEST("tui_layout_leaf creates",     leaf != NULL);

    tui_layout_free(vbox);
    tui_layout_free(hbox);
    tui_layout_free(leaf);

    /* free(NULL) should be safe */
    tui_layout_free(NULL);
    TEST("tui_layout_free(NULL) safe",  1);
}

static int dummy_leaf_render(int fd, const tui_rect_t *area, void *data)
{
    (void)fd; (void)area; (void)data;
    return TUI_OK;
}

static void test_layout_add(void)
{
    tui_layout_t *vbox = tui_layout_vbox(1);
    TEST("layout_vbox created", vbox != NULL);
    if (!vbox) return;

    tui_layout_t *c1 = tui_layout_leaf(dummy_leaf_render, NULL);
    tui_layout_t *c2 = tui_layout_leaf(dummy_leaf_render, NULL);
    tui_layout_t *c3 = tui_layout_leaf(dummy_leaf_render, NULL);
    TEST("children created", c1 && c2 && c3);

    TEST("tui_layout_add OK",       tui_layout_add(vbox, c1, 1) == TUI_OK);
    TEST("tui_layout_add weight 0", tui_layout_add(vbox, c2, 0) == TUI_OK);
    TEST("tui_layout_add weight 2", tui_layout_add(vbox, c3, 2) == TUI_OK);

    /* add to leaf should fail */
    TEST("add to leaf fails",       tui_layout_add(c1, c2, 1) == TUI_ERR_PARAM);

    /* add NULL should fail */
    TEST("add NULL parent fails",   tui_layout_add(NULL, c2, 1) == TUI_ERR_PARAM);
    TEST("add NULL child fails",    tui_layout_add(vbox, NULL, 1) == TUI_ERR_PARAM);

    tui_layout_free(vbox);
}

static void test_layout_valid(void)
{
    TEST("tui_layout_valid(NULL)",      !tui_layout_valid(NULL));

    tui_layout_t *leaf = tui_layout_leaf(NULL, NULL);
    TEST("leaf with NULL fn invalid",   !tui_layout_valid(leaf));

    tui_layout_free(leaf);

    leaf = tui_layout_leaf(dummy_leaf_render, NULL);
    TEST("leaf with fn valid",          tui_layout_valid(leaf));

    tui_layout_t *vbox = tui_layout_vbox(1);
    TEST("empty vbox invalid",          !tui_layout_valid(vbox));
    tui_layout_free(vbox);
    tui_layout_free(leaf);
}

static void test_layout_render(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe creation", 0); return; }

    tui_layout_t *vbox = tui_layout_vbox(0);
    TEST("layout created", vbox != NULL);
    if (!vbox) { close(fd); return; }

    tui_layout_t *c1 = tui_layout_leaf(dummy_leaf_render, NULL);
    tui_layout_t *c2 = tui_layout_leaf(dummy_leaf_render, NULL);
    tui_layout_add(vbox, c1, 1);
    tui_layout_add(vbox, c2, 1);

    tui_rect_t area = { 1, 1, 24, 80 };
    TEST("layout_render OK",           tui_layout_render(fd, vbox, area) == TUI_OK);

    /* NULL root should fail */
    TEST("layout_render NULL fails",   tui_layout_render(fd, NULL, area) == TUI_ERR_PARAM);

    /* invalid area should fail */
    tui_rect_t bad = { 1, 1, 0, 80 };
    TEST("layout_render bad area",     tui_layout_render(fd, vbox, bad) == TUI_ERR_PARAM);

    tui_layout_free(vbox);
    close(fd);
}

/* ══════════════════════════════════════════════════════
 *  Widget 测试
 * ══════════════════════════════════════════════════════ */

static void test_spaces(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe", 0); return; }

    TEST("tui_spaces 0", tui_spaces(fd, 0) == TUI_OK);
    TEST("tui_spaces 10", tui_spaces(fd, 10) == TUI_OK);
    TEST("tui_spaces 100", tui_spaces(fd, 100) == TUI_OK);

    close(fd);
}

static void test_hline(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe", 0); return; }

    TEST("tui_hline 0", tui_hline(fd, 1, 0, '-', TUI_COLOR_DEFAULT) == TUI_OK);
    TEST("tui_hline 20", tui_hline(fd, 1, 20, '=', TUI_COLOR_GREEN) == TUI_OK);

    close(fd);
}

static void test_cprintf(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe", 0); return; }

    TEST("tui_cprintf default", tui_cprintf(fd, TUI_COLOR_DEFAULT, TUI_COLOR_DEFAULT, "hello") == TUI_OK);
    TEST("tui_cprintf color",   tui_cprintf(fd, TUI_COLOR_GREEN, TUI_COLOR_BLACK, "green on black") == TUI_OK);

    close(fd);
}

static void test_fill_rect(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe", 0); return; }

    tui_rect_t r = { 5, 10, 3, 20 };
    TEST("tui_fill_rect", tui_fill_rect(fd, r, TUI_COLOR_GREEN) == TUI_OK);

    close(fd);
}

static void test_styled_text(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe", 0); return; }

    tui_rect_t r = { 1, 1, 1, 30 };
    tui_style_t s = { TUI_COLOR_WHITE, TUI_COLOR_GREEN, TUI_ATTR_BOLD };
    TEST("tui_styled_text", tui_styled_text(fd, r, "Hello TUI", s) == TUI_OK);

    close(fd);
}

static void test_progress(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe", 0); return; }

    tui_progress_t p1;
    memset(&p1, 0, sizeof(p1));
    p1.value = 0.5;
    strcpy(p1.label, "test");

    tui_rect_t r = { 1, 1, 1, 40 };
    TEST("tui_progress_render", tui_progress_render(fd, &r, &p1) == TUI_OK);

    /* full progress */
    p1.value = 1.0;
    p1.show_percent = 1;
    TEST("tui_progress_render full", tui_progress_render(fd, &r, &p1) == TUI_OK);

    /* zero progress */
    p1.value = 0.0;
    TEST("tui_progress_render zero", tui_progress_render(fd, &r, &p1) == TUI_OK);

    close(fd);
}

static void test_spinner(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe", 0); return; }

    tui_spinner_t s;
    memset(&s, 0, sizeof(s));
    s.color = TUI_COLOR_GREEN;

    tui_rect_t r = { 1, 1, 1, 5 };
    TEST("tui_spinner_render",  tui_spinner_render(fd, &r, &s) == TUI_OK);

    tui_spinner_tick(&s);
    TEST("spinner frame advanced", s.frame == 1);

    tui_spinner_tick(&s);
    TEST("spinner frame 2", s.frame == 2);

    tui_spinner_tick(NULL);
    TEST("tui_spinner_tick(NULL) safe", s.frame == 2);

    close(fd);
}

static void test_statusbar(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe", 0); return; }

    tui_statusbar_t sb;
    memset(&sb, 0, sizeof(sb));
    strcpy(sb.left, "READY");
    strcpy(sb.right, "help: F1");
    sb.bg = TUI_COLOR_GREEN;
    sb.fg = TUI_COLOR_WHITE;

    tui_rect_t r = { 1, 1, 1, 40 };
    TEST("tui_statusbar_render", tui_statusbar_render(fd, &r, &sb) == TUI_OK);

    close(fd);
}

static void test_panel(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe", 0); return; }

    tui_layout_t *panel = tui_panel_new("Test Panel", dummy_leaf_render, NULL);
    TEST("tui_panel_new", panel != NULL);
    if (!panel) { close(fd); return; }

    tui_rect_t r = { 1, 1, 10, 40 };
    TEST("panel layout_render", tui_layout_render(fd, panel, r) == TUI_OK);

    tui_layout_free(panel);

    /* styled panel */
    tui_panel_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.border_style = 2;  /* rounded */
    cfg.border_color = TUI_COLOR_CYAN;
    cfg.title_color  = TUI_COLOR_CYAN;
    strcpy(cfg.title, "Styled");
    cfg.content_fn   = dummy_leaf_render;

    tui_layout_t *sp = tui_panel_new_styled(&cfg);
    TEST("tui_panel_new_styled", sp != NULL);
    if (sp) {
        TEST("styled panel render", tui_layout_render(fd, sp, r) == TUI_OK);
        tui_layout_free(sp);
    }

    close(fd);
}

static void test_app_layout(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe", 0); return; }

    tui_layout_t *app = tui_app_layout(
        "  MeuOS TUI Demo  ",
        dummy_leaf_render, NULL,
        "READY | line 1/100",
        "v1.0.0"
    );
    TEST("tui_app_layout", app != NULL);
    if (!app) { close(fd); return; }

    tui_rect_t r = { 1, 1, 24, 80 };
    TEST("app layout render", tui_layout_render(fd, app, r) == TUI_OK);

    tui_layout_free(app);
    close(fd);
}

static void test_split_layout(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe", 0); return; }

    tui_layout_t *split = tui_split_layout(
        20, dummy_leaf_render, NULL,
        dummy_leaf_render, NULL
    );
    TEST("tui_split_layout", split != NULL);
    if (!split) { close(fd); return; }

    tui_rect_t r = { 1, 1, 24, 80 };
    TEST("split layout render", tui_layout_render(fd, split, r) == TUI_OK);

    tui_layout_free(split);
    close(fd);
}

static void test_pad_layout(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe", 0); return; }

    tui_layout_t *vbox = tui_layout_vbox(1);
    TEST("vbox created", vbox != NULL);
    if (!vbox) { close(fd); return; }

    tui_layout_pad(vbox, 2, 4, 2, 4);
    tui_layout_t *leaf = tui_layout_leaf(dummy_leaf_render, NULL);
    tui_layout_add(vbox, leaf, 1);

    tui_rect_t r = { 1, 1, 24, 80 };
    TEST("padded layout render", tui_layout_render(fd, vbox, r) == TUI_OK);

    tui_layout_free(vbox);
    close(fd);
}

/* ══════════════════════════════════════════════════════
 *  新 Widget 测试
 * ══════════════════════════════════════════════════════ */

static void test_label(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe", 0); return; }

    tui_rect_t r = { 1, 1, 1, 20 };

    tui_layout_t *lbl = tui_label_new("Hello Label", TUI_COLOR_GREEN);
    TEST("tui_label_new", lbl != NULL);
    if (lbl) {
        TEST("label render", tui_layout_render(fd, lbl, r) == TUI_OK);
        tui_layout_free(lbl);
    }

    tui_layout_t *h = tui_heading("Section Title", TUI_COLOR_CYAN);
    TEST("tui_heading", h != NULL);
    if (h) {
        TEST("heading render", tui_layout_render(fd, h, r) == TUI_OK);
        tui_layout_free(h);
    }

    close(fd);
}

static void test_separator(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe", 0); return; }

    tui_rect_t r = { 1, 1, 1, 30 };

    tui_layout_t *hr = tui_hr(TUI_COLOR_GREEN);
    TEST("tui_hr", hr != NULL);
    if (hr) {
        TEST("hr render", tui_layout_render(fd, hr, r) == TUI_OK);
        tui_layout_free(hr);
    }

    tui_layout_t *hrl = tui_hr_label("Section", TUI_COLOR_CYAN);
    TEST("tui_hr_label", hrl != NULL);
    if (hrl) {
        TEST("hr_label render", tui_layout_render(fd, hrl, r) == TUI_OK);
        tui_layout_free(hrl);
    }

    close(fd);
}

static void test_badge(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe", 0); return; }

    tui_rect_t r = { 1, 1, 1, 10 };

    tui_layout_t *b = tui_badge_new("v1.0", TUI_COLOR_GREEN);
    TEST("tui_badge_new", b != NULL);
    if (b) {
        TEST("badge render", tui_layout_render(fd, b, r) == TUI_OK);
        tui_layout_free(b);
    }

    close(fd);
}

static const char *test_cell_fn(int row, int col, void *udata)
{
    (void)udata;
    static char buf[32];
    snprintf(buf, sizeof(buf), "R%dC%d", row + 1, col + 1);
    return buf;
}

static void test_table(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe", 0); return; }

    tui_column_t cols[3] = {
        { "Name",  12, -1 },
        { "Value", 10,  0 },
        { "Status", 8,  0 },
    };

    tui_table_t tbl;
    memset(&tbl, 0, sizeof(tbl));
    memcpy(tbl.columns, cols, sizeof(cols));
    tbl.ncols   = 3;
    tbl.nrows   = 5;
    tbl.cell_fn = test_cell_fn;
    tbl.header_bg = TUI_COLOR_GREEN;
    tbl.select_bg = TUI_COLOR_GREEN;
    tbl.selected  = 2;

    tui_rect_t r = { 1, 1, 8, 40 };
    TEST("tui_table_render", tui_table_render(fd, &r, &tbl) == TUI_OK);

    /* handle */
    tui_event_t ev;
    ev.key = TUI_KEY_DOWN;
    TEST("table handle down", tui_table_handle(&tbl, &ev) == 1);
    TEST("table selected=3", tbl.selected == 3);

    ev.key = TUI_KEY_UP;
    tui_table_handle(&tbl, &ev);
    TEST("table selected=2 after up", tbl.selected == 2);

    close(fd);
}

static void test_draw_border(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe", 0); return; }

    tui_rect_t r = { 1, 1, 10, 40 };
    TEST("tui_draw_border", tui_draw_border(fd, &r, "Test", 0, TUI_COLOR_GREEN) == TUI_OK);
    TEST("border shrinks area", r.row == 2);
    TEST("border shrinks cols", r.cols == 38);

    close(fd);
}

static void test_list_widget(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe", 0); return; }

    tui_list_item_t items[] = {
        { "Item One",   "details1", NULL, 0 },
        { "Item Two",   "details2", NULL, 0 },
        { "Item Three", "details3", NULL, 0 },
    };

    tui_list_t *list = tui_list_new(items, 3);
    TEST("tui_list_new", list != NULL);
    TEST("list selected=0", tui_list_selected(list) == 0);

    tui_rect_t r = { 1, 1, 10, 40 };
    TEST("list render", tui_list_render(fd, &r, list) == TUI_OK);

    tui_event_t ev;
    ev.key = TUI_KEY_DOWN;
    tui_list_handle(list, &ev);
    TEST("list handle down", tui_list_selected(list) == 1);

    ev.key = TUI_KEY_UP;
    tui_list_handle(list, &ev);
    TEST("list handle up", tui_list_selected(list) == 0);

    ev.key = TUI_KEY_END;
    tui_list_handle(list, &ev);
    TEST("list handle end", tui_list_selected(list) == 2);

    tui_list_free(list);
    close(fd);
}

static void test_list_layout(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe", 0); return; }

    tui_list_item_t items[] = {
        { "Alpha", "", NULL, 0 },
        { "Beta",  "", NULL, 0 },
    };
    tui_list_t *list = tui_list_new(items, 2);
    TEST("tui_list_layout", list != NULL);

    tui_layout_t *lay = tui_list_layout(list);
    TEST("list_layout node", lay != NULL);
    if (lay) {
        tui_rect_t r = { 1, 1, 5, 30 };
        TEST("list layout render", tui_layout_render(fd, lay, r) == TUI_OK);
        tui_layout_free(lay);
        /* list freed by layout */
    }

    close(fd);
}

static void test_dialog_render(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe", 0); return; }

    tui_layout_t *d = tui_dialog_layout("Confirm", "Are you sure?",
                                        TUI_DLG_QUESTION, TUI_DLG_YES | TUI_DLG_NO);
    TEST("tui_dialog_layout", d != NULL);
    if (d) {
        tui_rect_t r = { 1, 1, 10, 45 };
        TEST("dialog render", tui_layout_render(fd, d, r) == TUI_OK);
        tui_layout_free(d);
    }

    close(fd);
}

static void test_input_widget(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe", 0); return; }

    tui_input_t inp;
    memset(&inp, 0, sizeof(inp));
    inp.active = 1;
    inp.cursor = 0;
    strcpy(inp.prompt, ">");
    strcpy(inp.buffer, "test input");

    tui_event_t ev;

    /* render */
    tui_rect_t r = { 1, 1, 1, 30 };
    TEST("input render", tui_input_render(fd, &r, &inp) == TUI_OK);

    /* handle: type char */
    ev.key = (tui_key_t)'X';
    tui_input_handle(&inp, &ev);
    TEST("input handle char", strcmp(inp.buffer, "Xtest input") == 0);

    /* handle: backspace */
    ev.key = TUI_KEY_BS;
    tui_input_handle(&inp, &ev);
    TEST("input handle bs", strcmp(inp.buffer, "test input") == 0);

    /* handle: Enter */
    ev.key = TUI_KEY_CR;
    tui_input_handle(&inp, &ev);
    TEST("input done", tui_input_done(&inp));

    /* layout */
    tui_input_reset(&inp);
    TEST("input reset active", inp.active == 1);

    tui_layout_t *lay = tui_input_layout(&inp);
    TEST("input_layout", lay != NULL);
    if (lay) {
        TEST("input layout render", tui_layout_render(fd, lay, r) == TUI_OK);
        tui_layout_free(lay);
    }

    close(fd);
}

static void test_display_modes(void)
{
    int fd = test_write_to_pipe();
    if (fd < 0) { TEST("pipe", 0); return; }

    tui_rect_t area = { 1, 1, 24, 80 };

    /* fullscreen */
    tui_layout_t *fs = tui_layout_fullscreen(dummy_leaf_render, NULL);
    TEST("fullscreen", fs != NULL);
    if (fs) {
        TEST("fullscreen render", tui_layout_render(fd, fs, area) == TUI_OK);
        tui_layout_free(fs);
    }

    /* centered */
    tui_layout_t *ct = tui_layout_centered(40, 10, dummy_leaf_render, NULL);
    TEST("centered", ct != NULL);
    if (ct) {
        TEST("centered render", tui_layout_render(fd, ct, area) == TUI_OK);
        tui_layout_free(ct);
    }

    /* wizard */
    tui_layout_t *wz = tui_layout_wizard("Setup Wizard", dummy_leaf_render, NULL, "Step 1/3");
    TEST("wizard", wz != NULL);
    if (wz) {
        TEST("wizard render", tui_layout_render(fd, wz, area) == TUI_OK);
        tui_layout_free(wz);
    }

    /* dual */
    tui_layout_t *dl = tui_layout_dual(20, "Nav",
                                        dummy_leaf_render, NULL,
                                        dummy_leaf_render, NULL);
    TEST("dual layout", dl != NULL);
    if (dl) {
        TEST("dual render", tui_layout_render(fd, dl, area) == TUI_OK);
        tui_layout_free(dl);
    }

    close(fd);
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

    /* 布局系统 */
    printf("[layout]\n");
    test_rect_valid();
    test_layout_create_free();
    test_layout_add();
    test_layout_valid();
    test_layout_render();
    printf("\n");

    /* Widgets */
    printf("[widget]\n");
    test_spaces();
    test_hline();
    test_cprintf();
    test_fill_rect();
    test_styled_text();
    test_progress();
    test_spinner();
    test_statusbar();
    test_panel();
    printf("\n");

    /* 模板布局 */
    printf("[template]\n");
    test_app_layout();
    test_split_layout();
    test_pad_layout();
    printf("\n");

    /* 新 Widget */
    printf("[new-widget]\n");
    test_label();
    test_separator();
    test_badge();
    test_table();
    test_draw_border();
    printf("\n");

    /* 列表 */
    printf("[list]\n");
    test_list_widget();
    test_list_layout();
    printf("\n");

    /* 对话框 */
    printf("[dialog]\n");
    test_dialog_render();
    printf("\n");

    /* 输入框 */
    printf("[input-widget]\n");
    test_input_widget();
    printf("\n");

    /* 显示模式 */
    printf("[display-modes]\n");
    test_display_modes();
    printf("\n");

    /* 汇总 */
    printf("========================\n");
    printf("passed: %d, failed: %d\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
