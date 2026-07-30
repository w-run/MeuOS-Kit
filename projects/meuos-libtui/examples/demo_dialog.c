/* demo_dialog.c — 对话框交互演示
 *
 * 循环展示 Info / Warning / Error / Question / Input 五种对话框，
 * 配合居中布局模板，模拟安装器/配置工具场景。
 * 运行方式: make demo_dialog && ./build/demo_dialog
 */

#define _XOPEN_SOURCE 700

#include "meuos/libtui.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* ══════════════════════════════════════════════════════
 *  模拟内容区：显示当前步骤说明
 * ══════════════════════════════════════════════════════ */

static int step_content(int fd, const tui_rect_t *area, void *udata)
{
    const char *msg = (const char *)udata;
    if (!msg) msg = "Loading...";

    int x = area->col + 2;
    int y = area->row;

    tui_cursor_goto(fd, y, x);
    tui_set_fg(fd, tui_meuos_theme.fg);
    tui_set_attr(fd, TUI_ATTR_DIM);
    tui_write(fd, "Step status:");
    tui_reset_style(fd);
    y++;

    tui_cursor_goto(fd, y, x);
    tui_set_fg(fd, tui_meuos_theme.accent);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_write(fd, msg);
    tui_reset_style(fd);

    return TUI_OK;
}

/* ══════════════════════════════════════════════════════
 *  main
 * ══════════════════════════════════════════════════════ */

int main(void)
{
    /* 进入终端模式 */
    tui_raw_mode(0, 1);
    tui_alt_screen(0, 1);
    tui_cursor_show(0, 0);

    tui_size_t size;
    tui_get_size(0, &size);

    /* ── 步骤 1: Info 弹窗 ── */
    tui_clear_screen(0);
    tui_layout_t *wz1 = tui_layout_wizard(
        "  Setup Wizard  ", step_content, "Initializing...", "Step 1/5");
    tui_rect_t full = { 1, 1, size.rows, size.cols };
    tui_layout_render(0, wz1, full);

    int btn = tui_dialog_blocking(0,
        (tui_rect_t){ size.rows/2-5, size.cols/2-20, 12, 44 },
        "Welcome",
        "Welcome to MeuOS Kit setup!\nThis wizard will guide you\nthrough the installation.",
        TUI_DLG_INFO, TUI_DLG_OK);

    tui_layout_free(wz1);

    if (btn != TUI_DLG_OK && btn != TUI_DLG_YES) goto cleanup;

    /* ── 步骤 2: 确认对话框 ── */
    tui_clear_screen(0);
    tui_layout_t *wz2 = tui_layout_wizard(
        "  Setup Wizard  ", step_content, "Downloading packages...", "Step 2/5");
    tui_layout_render(0, wz2, full);

    btn = tui_dialog_blocking(0,
        (tui_rect_t){ size.rows/2-4, size.cols/2-18, 10, 40 },
        "Confirm",
        "Download 42 packages?\nTotal size: 256 MB",
        TUI_DLG_QUESTION, TUI_DLG_YES | TUI_DLG_NO);

    tui_layout_free(wz2);

    if (btn != TUI_DLG_OK && btn != TUI_DLG_YES) goto cleanup;

    /* ── 步骤 3: 进度 + 警告 ── */
    tui_clear_screen(0);
    tui_layout_t *wz3 = tui_layout_wizard(
        "  Setup Wizard  ", step_content, "Installing... (67%)", "Step 3/5");
    tui_layout_render(0, wz3, full);

    btn = tui_dialog_blocking(0,
        (tui_rect_t){ size.rows/2-4, size.cols/2-20, 10, 44 },
        "Warning",
        "Low disk space: only 1.2 GB left.\nContinue anyway?",
        TUI_DLG_WARNING, TUI_DLG_YES | TUI_DLG_NO);

    tui_layout_free(wz3);

    if (btn != TUI_DLG_OK && btn != TUI_DLG_YES) goto cleanup;

    /* ── 步骤 4: 输入对话框 ── */
    tui_clear_screen(0);
    tui_layout_t *wz4 = tui_layout_wizard(
        "  Setup Wizard  ", step_content, "Configuration...", "Step 4/5");
    tui_layout_render(0, wz4, full);

    btn = tui_dialog_blocking(0,
        (tui_rect_t){ size.rows/2-5, size.cols/2-20, 12, 44 },
        "User Input",
        "Enter your username:",
        TUI_DLG_INPUT, TUI_DLG_OK | TUI_DLG_CANCEL);

    tui_layout_free(wz4);

    if (btn != TUI_DLG_OK && btn != TUI_DLG_YES) goto cleanup;

    /* ── 步骤 5: 错误 + 完成 ── */
    tui_clear_screen(0);
    tui_layout_t *wz5 = tui_layout_wizard(
        "  Setup Wizard  ", step_content, "Finalizing...", "Step 5/5");
    tui_layout_render(0, wz5, full);

    btn = tui_dialog_blocking(0,
        (tui_rect_t){ size.rows/2-4, size.cols/2-20, 10, 44 },
        "Error",
        "Network error: connection timeout.\nPlease check your connection.",
        TUI_DLG_ERROR, TUI_DLG_RETRY | TUI_DLG_CANCEL);

    tui_layout_free(wz5);

cleanup:
    /* 再见画面 */
    tui_clear_screen(0);
    tui_layout_t *bye = tui_layout_centered(30, 5, step_content,
        "  Thank you for using MeuOS Kit!  ");
    tui_layout_render(0, bye, full);
    tui_layout_free(bye);

    sleep(1);

    /* 清理 */
    tui_cursor_show(0, 1);
    tui_alt_screen(0, 0);
    tui_raw_mode(0, 0);
    tui_clear_screen(0);

    return 0;
}
