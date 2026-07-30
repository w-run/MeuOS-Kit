/* demo_dialog.c — 对话框交互演示（安装向导场景）
 *
 * 循环展示 Info / Warning / Error / Question / Input 五种对话框。
 * 运行: make demo_dialog && ./build/demo_dialog
 */

#define _XOPEN_SOURCE 700

#include "meuos/libtui.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* ══════════════════════════════════════════════════════
 *  步骤说明区回调
 * ══════════════════════════════════════════════════════ */

static int step_content(int fd, const tui_rect_t *area, void *udata)
{
    const char *msg = (const char *)udata;
    if (!msg) msg = "Loading...";

    int y = area->row + 1;
    int x = area->col + 2;

    tui_set_attr(fd, TUI_ATTR_DIM);
    tui_cprintf(fd, TUI_COLOR_DEFAULT, TUI_COLOR_DEFAULT, "  Step status:");
    y++;

    tui_cursor_goto(fd, y, x);
    tui_set_fg(fd, tui_meuos_theme.accent);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_write(fd, msg);
    tui_reset_style(fd);

    return TUI_OK;
}

/* ══════════════════════════════════════════════════════
 *  阻塞对话框包装
 * ══════════════════════════════════════════════════════ */

static int blocking_dlg(tui_size_t scr, const char *title, const char *msg,
                        tui_dlg_type_t type, int buttons)
{
    int dlg_w = 50;
    int dlg_h = 10;
    if (dlg_w > scr.cols - 4) dlg_w = scr.cols - 4;
    if (dlg_h > scr.rows - 4) dlg_h = scr.rows - 4;

    int dr = (scr.rows - dlg_h) / 2;
    int dc = (scr.cols - dlg_w) / 2;
    if (dr < 1) dr = 1;
    if (dc < 1) dc = 1;

    return tui_dialog_blocking(STDIN_FILENO,
        (tui_rect_t){ dr, dc, dlg_h, dlg_w },
        title, msg, type, buttons);
}

/* ══════════════════════════════════════════════════════
 *  main
 * ══════════════════════════════════════════════════════ */

int main(void)
{
    int ofd = STDOUT_FILENO;
    int ifd = STDIN_FILENO;

    tui_raw_mode(ifd, 1);
    tui_clear_screen(ofd);
    tui_cursor_show(ofd, 0);

    tui_size_t scr;
    if (tui_get_size(ofd, &scr) != TUI_OK) {
        scr.rows = 24;
        scr.cols = 80;
    }

    int btn;

    /* ── Step 1: Info ── */
    tui_clear_screen(ofd);
    tui_layout_t *wz = tui_layout_wizard(
        "  Install Wizard  ", step_content, "Initializing ...",
        "Step 1/5");
    tui_rect_t full = { 1, 1, scr.rows, scr.cols - 1 };
    tui_layout_render(ofd, wz, full);

    btn = blocking_dlg(scr, "Welcome",
        "Welcome to MeuOS Kit setup.\nThis wizard will guide you\nthrough the installation.",
        TUI_DLG_INFO, TUI_DLG_OK);

    tui_layout_free(wz);
    if (btn != TUI_DLG_OK) goto cleanup;

    /* ── Step 2: Question ── */
    tui_clear_screen(ofd);
    wz = tui_layout_wizard(
        "  Install Wizard  ", step_content,
        "Downloading packages ...", "Step 2/5");
    tui_layout_render(ofd, wz, full);

    btn = blocking_dlg(scr, "Confirm",
        "Download 42 packages?\nTotal transfer: 256 MB",
        TUI_DLG_QUESTION, TUI_DLG_YES | TUI_DLG_NO);

    tui_layout_free(wz);
    if (btn != TUI_DLG_YES) goto cleanup;

    /* ── Step 3: Warning ── */
    tui_clear_screen(ofd);
    wz = tui_layout_wizard(
        "  Install Wizard  ", step_content,
        "Installing ... (67 %)", "Step 3/5");
    tui_layout_render(ofd, wz, full);

    btn = blocking_dlg(scr, "Warning",
        "Low disk space: only 1.2 GB left.\nContinue anyway?",
        TUI_DLG_WARNING, TUI_DLG_YES | TUI_DLG_NO);

    tui_layout_free(wz);
    if (btn != TUI_DLG_YES) goto cleanup;

    /* ── Step 4: Input ── */
    tui_clear_screen(ofd);
    wz = tui_layout_wizard(
        "  Install Wizard  ", step_content,
        "Configuration ...", "Step 4/5");
    tui_layout_render(ofd, wz, full);

    btn = blocking_dlg(scr, "User Name",
        "Enter your username:",
        TUI_DLG_INPUT, TUI_DLG_OK | TUI_DLG_CANCEL);

    tui_layout_free(wz);
    if (btn != TUI_DLG_OK) goto cleanup;

    /* ── Step 5: Error ── */
    tui_clear_screen(ofd);
    wz = tui_layout_wizard(
        "  Install Wizard  ", step_content,
        "Finalizing ...", "Step 5/5");
    tui_layout_render(ofd, wz, full);

    btn = blocking_dlg(scr, "Network Error",
        "Connection timeout.\nPlease check your network.",
        TUI_DLG_ERROR, TUI_DLG_RETRY | TUI_DLG_CANCEL);

    tui_layout_free(wz);

cleanup:
    tui_clear_screen(ofd);
    tui_cursor_goto(ofd, scr.rows/2, scr.cols/2 - 10);
    tui_set_fg(ofd, tui_meuos_theme.accent);
    tui_set_attr(ofd, TUI_ATTR_BOLD);
    tui_write(ofd, "Thanks for trying MeuOS Kit !");
    tui_reset_style(ofd);
    sleep(1);

    tui_cursor_show(ofd, 1);
    tui_clear_screen(ofd);
    tui_raw_mode(ifd, 0);

    return 0;
}
