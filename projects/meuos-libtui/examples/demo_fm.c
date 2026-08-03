/* demo_fm.c — TUI 文件管理器
 *
 * 双栏布局：左侧目录树列表 + 右侧文件详情
 * 支持：目录导航、文件信息显示、选中高亮
 *
 * 按键: ↑↓=导航  Enter=进入目录  ←=上级  q/ESC=退出
 */

#define _XOPEN_SOURCE 700

#include "meuos/libtui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

#define MAX_ENTRIES 256

typedef struct {
    char name[256];
    char size_str[32];
    char date_str[32];
    char mode_str[16];
    int  is_dir;
    long size;
} fm_entry_t;

typedef struct {
    fm_entry_t entries[MAX_ENTRIES];
    int        count;
    int        selected;
    char       cwd[512];
} fm_state_t;

static void format_size(long sz, char *out, int max) {
    if (sz < 1024) snprintf(out, max, "%ld B", sz);
    else if (sz < 1024*1024) snprintf(out, max, "%.1f K", sz/1024.0);
    else if (sz < 1024*1024*1024) snprintf(out, max, "%.1f M", sz/(1024.0*1024));
    else snprintf(out, max, "%.1f G", sz/(1024.0*1024*1024));
}

static void format_mode(mode_t m, char *out, int max) {
    snprintf(out, max, "%c%c%c%c%c%c%c%c%c%c",
        S_ISDIR(m) ? 'd' : S_ISLNK(m) ? 'l' : '-',
        m & 0400 ? 'r' : '-', m & 0200 ? 'w' : '-', m & 0100 ? 'x' : '-',
        m & 0040 ? 'r' : '-', m & 0020 ? 'w' : '-', m & 0010 ? 'x' : '-',
        m & 0004 ? 'r' : '-', m & 0002 ? 'w' : '-', m & 0001 ? 'x' : '-');
}

static void format_date(time_t t, char *out, int max) {
    struct tm *tm = localtime(&t);
    strftime(out, max, "%m-%d %H:%M", tm);
}

static int cmp_entries(const void *a, const void *b) {
    const fm_entry_t *ea = a, *eb = b;
    if (ea->is_dir != eb->is_dir) return ea->is_dir ? -1 : 1;
    return strcmp(ea->name, eb->name);
}

static void scan_dir(fm_state_t *fm, const char *path) {
    DIR *d = opendir(path);
    if (!d) return;

    strncpy(fm->cwd, path, sizeof(fm->cwd) - 1);
    fm->count = 0;
    fm->selected = 0;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && fm->count < MAX_ENTRIES) {
        if (ent->d_name[0] == '.' && ent->d_name[1] != '.' && ent->d_name[1] != '\0')
            continue;

        fm_entry_t *e = &fm->entries[fm->count];
        strncpy(e->name, ent->d_name, sizeof(e->name) - 1);
        e->name[sizeof(e->name) - 1] = '\0';

        char full[768];
        snprintf(full, sizeof(full), "%s/%s", path, ent->d_name);
        struct stat st;
        if (lstat(full, &st) == 0) {
            e->is_dir = S_ISDIR(st.st_mode);
            e->size = st.st_size;
            format_size(st.st_size, e->size_str, sizeof(e->size_str));
            format_mode(st.st_mode, e->mode_str, sizeof(e->mode_str));
            format_date(st.st_mtime, e->date_str, sizeof(e->date_str));
        } else {
            e->is_dir = 0; e->size = 0;
            strcpy(e->size_str, "---"); strcpy(e->mode_str, "----------");
            strcpy(e->date_str, "--/--");
        }
        fm->count++;
    }
    closedir(d);
    qsort(fm->entries, fm->count, sizeof(fm_entry_t), cmp_entries);
}

/* ── 渲染 ── */

static int fm_list_render(int fd, const tui_rect_t *area, void *udata) {
    fm_state_t *fm = (fm_state_t *)udata;
    tui_rect_t inner = *area;
    tui_draw_border(fd, &inner, "  Files  ", 0, tui_meuos_theme.border);
    if (!tui_rect_valid(&inner)) return TUI_OK;

    int max_lines = inner.rows;
    int start = 0;
    if (fm->selected >= max_lines) start = fm->selected - max_lines + 1;

    for (int i = 0; i < max_lines && start + i < fm->count; i++) {
        int idx = start + i;
        int is_sel = (idx == fm->selected);
        tui_cursor_goto(fd, inner.row + i, inner.col);

        if (is_sel) {
            tui_set_bg(fd, tui_meuos_theme.highlight);
            tui_set_fg(fd, TUI_COLOR_WHITE);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            tui_spaces(fd, inner.cols);
            tui_cursor_goto(fd, inner.row + i, inner.col);
        }

        fm_entry_t *e = &fm->entries[idx];

        /* 图标 */
        if (is_sel) { tui_set_bg(fd, tui_meuos_theme.highlight); tui_set_fg(fd, TUI_COLOR_YELLOW); }
        else tui_set_fg(fd, e->is_dir ? TUI_COLOR_CYAN : TUI_COLOR_DEFAULT);
        tui_write(fd, e->is_dir ? "DIR " : "    ");

        /* 名称 */
        if (is_sel) { tui_set_bg(fd, tui_meuos_theme.highlight); tui_set_fg(fd, e->is_dir ? TUI_COLOR_CYAN : TUI_COLOR_WHITE); }
        else tui_set_fg(fd, e->is_dir ? TUI_COLOR_CYAN : TUI_COLOR_DEFAULT);
        if (e->is_dir) tui_set_attr(fd, TUI_ATTR_BOLD);
        int bytes = tui_truncate(e->name, inner.cols - 20);
        write(fd, e->name, (size_t)bytes);

        /* 大小 */
        if (!is_sel) tui_reset_style(fd);
        tui_set_fg(fd, tui_meuos_theme.dim);
        int name_w = tui_strwidth(e->name);
        int size_x = inner.col + 20;
        if (size_x < inner.col + inner.cols) {
            tui_cursor_goto(fd, inner.row + i, size_x);
            if (is_sel) { tui_set_bg(fd, tui_meuos_theme.highlight); }
            tui_write(fd, e->size_str);
        }

        /* 日期 */
        int date_x = inner.col + 32;
        if (date_x + 10 < inner.col + inner.cols) {
            tui_cursor_goto(fd, inner.row + i, date_x);
            if (is_sel) { tui_set_bg(fd, tui_meuos_theme.highlight); }
            tui_write(fd, e->date_str);
        }

        tui_reset_style(fd);
    }
    return TUI_OK;
}

static int fm_detail_render(int fd, const tui_rect_t *area, void *udata) {
    fm_state_t *fm = (fm_state_t *)udata;
    tui_rect_t inner = *area;
    tui_draw_border(fd, &inner, "  Details  ", 0, TUI_COLOR_CYAN);
    if (!tui_rect_valid(&inner)) return TUI_OK;

    if (fm->selected >= fm->count) return TUI_OK;
    fm_entry_t *e = &fm->entries[fm->selected];

    int y = inner.row;
    int x = inner.col + 2;

    /* 文件名 */
    tui_cursor_goto(fd, y, x);
    tui_set_fg(fd, e->is_dir ? TUI_COLOR_CYAN : TUI_COLOR_GREEN);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_write(fd, e->is_dir ? "[DIR] " : "[FILE] ");
    tui_write(fd, e->name);
    tui_reset_style(fd);
    y += 2;

    /* 属性表 */
    tui_cursor_goto(fd, y, x);
    tui_set_fg(fd, tui_meuos_theme.dim);
    tui_write(fd, "Size:  ");
    tui_reset_style(fd);
    tui_set_fg(fd, TUI_COLOR_YELLOW);
    tui_write(fd, e->size_str);
    y++;

    tui_cursor_goto(fd, y, x);
    tui_set_fg(fd, tui_meuos_theme.dim);
    tui_write(fd, "Date:  ");
    tui_reset_style(fd);
    tui_write(fd, e->date_str);
    y++;

    tui_cursor_goto(fd, y, x);
    tui_set_fg(fd, tui_meuos_theme.dim);
    tui_write(fd, "Mode:  ");
    tui_reset_style(fd);
    tui_set_fg(fd, TUI_COLOR_MAGENTA);
    tui_write(fd, e->mode_str);
    y += 2;

    /* 路径 */
    tui_cursor_goto(fd, y, x);
    tui_set_fg(fd, tui_meuos_theme.dim);
    tui_write(fd, "Path:");
    y++;
    tui_cursor_goto(fd, y, x);
    tui_set_fg(fd, TUI_COLOR_DEFAULT);
    int bytes = tui_truncate(fm->cwd, inner.cols - 4);
    write(fd, fm->cwd, (size_t)bytes);
    y += 2;

    /* 操作提示 */
    tui_cursor_goto(fd, y, x);
    tui_set_fg(fd, tui_meuos_theme.dim);
    tui_set_attr(fd, TUI_ATTR_DIM);
    tui_write(fd, "Enter=open  Left=up");
    tui_reset_style(fd);

    return TUI_OK;
}

int main(void) {
    tui_raw_mode(0, 1);
    tui_alt_screen(0, 1);
    tui_clear_screen(0);
    tui_cursor_show(0, 0);

    tui_size_t scr;
    if (tui_get_size(0, &scr) != TUI_OK) { scr.rows = 30; scr.cols = 80; }

    fm_state_t fm;
    memset(&fm, 0, sizeof(fm));

    /* 起始目录 */
    char start[512];
    if (!getcwd(start, sizeof(start)))
        strcpy(start, ".");
    scan_dir(&fm, start);

    tui_event_t ev;
    int running = 1;

    while (running) {
        /* 标题栏 */
        tui_cursor_goto(0, 1, 1);
        tui_set_bg(0, tui_meuos_theme.accent);
        tui_set_attr(0, TUI_ATTR_BOLD);
        tui_spaces(0, scr.cols - 1);
        tui_cursor_goto(0, 1, 3);
        tui_set_fg(0, TUI_COLOR_WHITE);
        tui_set_bg(0, tui_meuos_theme.accent);
        tui_write(0, "File Manager — MeuOS Kit");
        const char *hint = " q=quit  Up/Down=nav  Enter=open  Left=up ";
        int hw = tui_strwidth(hint);
        tui_cursor_goto(0, 1, scr.cols - hw - 1);
        tui_set_fg(0, TUI_COLOR_YELLOW);
        tui_set_bg(0, tui_meuos_theme.accent);
        tui_write(0, hint);
        tui_reset_style(0);

        /* 双栏布局 */
        int list_w = scr.cols * 3 / 5;
        tui_rect_t list_area = { 3, 1, scr.rows - 5, list_w };
        tui_rect_t detail_area = { 3, list_w + 2, scr.rows - 5, scr.cols - list_w - 3 };

        fm_list_render(0, &list_area, &fm);
        fm_detail_render(0, &detail_area, &fm);

        /* 状态栏 */
        tui_cursor_goto(0, scr.rows, 1);
        tui_set_bg(0, tui_meuos_theme.accent);
        tui_set_fg(0, TUI_COLOR_WHITE);
        tui_set_attr(0, TUI_ATTR_BOLD);
        char status[256];
        snprintf(status, sizeof(status), " %d items | %s ", fm.count, fm.cwd);
        tui_write(0, status);
        tui_reset_style(0);

        if (tui_getkey(0, &ev) == TUI_OK) {
            switch (ev.key) {
            case 'q': case TUI_KEY_ESC: running = 0; break;
            case TUI_KEY_UP: if (fm.selected > 0) fm.selected--; break;
            case TUI_KEY_DOWN: if (fm.selected < fm.count - 1) fm.selected++; break;
            case TUI_KEY_HOME: fm.selected = 0; break;
            case TUI_KEY_END: fm.selected = fm.count - 1; break;
            case TUI_KEY_LEFT:
                /* 上级目录 */
                {
                    char parent[512];
                    snprintf(parent, sizeof(parent), "%s/..", fm.cwd);
                    char real[512];
                    if (realpath(parent, real))
                        scan_dir(&fm, real);
                }
                break;
            case TUI_KEY_CR: case TUI_KEY_LF:
                if (fm.selected < fm.count && fm.entries[fm.selected].is_dir) {
                    char newpath[768];
                    snprintf(newpath, sizeof(newpath), "%s/%s", fm.cwd, fm.entries[fm.selected].name);
                    char real[512];
                    if (realpath(newpath, real))
                        scan_dir(&fm, real);
                }
                break;
            default: break;
            }
        }
    }

    tui_cursor_show(0, 1);
    tui_clear_screen(0);
    tui_alt_screen(0, 0);
    tui_raw_mode(0, 0);
    return 0;
}
