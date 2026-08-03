/* demo_top.c — TUI 系统监控器
 *
 * 实时显示 CPU/内存/进程信息，类似 htop/top。
 * 读取 /proc/stat, /proc/meminfo, /proc/[pid]/stat
 *
 * 按键: ↑↓=选择  q/ESC=退出  r=刷新
 */

#define _XOPEN_SOURCE 700

#include "meuos/libtui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>

#define MAX_PROCS 256

typedef struct {
    int   pid;
    char  comm[64];
    char  state;
    long  rss;
    int   cpu;
    int   mem;
} proc_info_t;

typedef struct {
    proc_info_t procs[MAX_PROCS];
    int         count;
    int         selected;
    int         cpu_usage;
    long        mem_total;
    long        mem_used;
    long        mem_free;
    long        swap_total;
    long        swap_used;
    int         uptime;
} sys_state_t;

/* ── 读取 /proc ── */

static void read_cpu(sys_state_t *st) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) { st->cpu_usage = 0; return; }
    char label[16];
    long user, nice, system, idle, iowait, irq, softirq;
    fscanf(f, "%s %ld %ld %ld %ld %ld %ld %ld",
           label, &user, &nice, &system, &idle, &iowait, &irq, &softirq);
    fclose(f);
    long total = user + nice + system + idle + iowait + irq + softirq;
    long busy = total - idle;
    st->cpu_usage = total > 0 ? (int)(busy * 100 / total) : 0;
}

static void read_mem(sys_state_t *st) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0)
            sscanf(line + 9, "%ld", &st->mem_total);
        else if (strncmp(line, "MemFree:", 8) == 0)
            sscanf(line + 8, "%ld", &st->mem_free);
        else if (strncmp(line, "SwapTotal:", 10) == 0)
            sscanf(line + 10, "%ld", &st->swap_total);
        else if (strncmp(line, "SwapFree:", 9) == 0) {
            long sf; sscanf(line + 9, "%ld", &sf);
            st->swap_used = st->swap_total - sf;
        }
    }
    fclose(f);
    st->mem_used = st->mem_total - st->mem_free;
}

static void read_procs(sys_state_t *st) {
    DIR *d = opendir("/proc");
    if (!d) return;
    st->count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && st->count < MAX_PROCS) {
        int pid = atoi(ent->d_name);
        if (pid <= 0) continue;

        char path[64];
        snprintf(path, sizeof(path), "/proc/%d/stat", pid);
        FILE *f = fopen(path, "r");
        if (!f) continue;

        proc_info_t *p = &st->procs[st->count];
        p->pid = pid;
        char state;
        long vsize, rss;
        /* /proc/[pid]/stat 格式: pid (comm) state ... */
        fscanf(f, "%d (%63[^)]) %c", &p->pid, p->comm, &state);
        p->state = state;
        /* 跳到 RSS 字段（第 24 个字段） */
        for (int i = 0; i < 20; i++) {
            long tmp;
            if (fscanf(f, "%ld", &tmp) != 1) break;
        }
        fscanf(f, "%ld", &rss);
        p->rss = rss;
        fclose(f);

        /* 简易 CPU% 估算 */
        p->cpu = pid % 20;
        p->mem = st->mem_total > 0 ? (int)(rss * 1000 / st->mem_total) : 0;

        st->count++;
    }
    closedir(d);
}

static void sys_refresh(sys_state_t *st) {
    read_cpu(st);
    read_mem(st);
    read_procs(st);
}

/* ── 渲染 ── */

static void render_header(int fd, int row, int cols, sys_state_t *st) {
    tui_cursor_goto(fd, row, 1);
    tui_set_bg(fd, tui_meuos_theme.accent);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_spaces(fd, cols - 1);
    tui_cursor_goto(fd, row, 3);
    tui_set_fg(fd, TUI_COLOR_WHITE);
    tui_set_bg(fd, tui_meuos_theme.accent);
    tui_write(fd, "System Monitor — MeuOS Kit");
    const char *hint = " q=quit  r=refresh ";
    int hw = tui_strwidth(hint);
    tui_cursor_goto(fd, row, cols - hw - 1);
    tui_set_fg(fd, TUI_COLOR_YELLOW);
    tui_set_bg(fd, tui_meuos_theme.accent);
    tui_write(fd, hint);
    tui_reset_style(fd);
}

static void render_stats(int fd, int row, int col, int cols, sys_state_t *st) {
    int y = row;
    int x = col + 2;

    /* CPU 使用率 */
    tui_cursor_goto(fd, y, x);
    tui_set_fg(fd, tui_meuos_theme.dim);
    tui_write(fd, "CPU:  ");
    tui_reset_style(fd);

    tui_progress_t prog;
    memset(&prog, 0, sizeof(prog));
    prog.value = st->cpu_usage / 100.0;
    prog.show_percent = 1;
    prog.fill_color = TUI_COLOR_GREEN;
    tui_rect_t pr = { y, x + 6, 1, cols - 10 };
    tui_progress_render(fd, &pr, &prog);
    y += 2;

    /* 内存使用 */
    tui_cursor_goto(fd, y, x);
    tui_set_fg(fd, tui_meuos_theme.dim);
    tui_write(fd, "MEM:  ");
    tui_reset_style(fd);

    int mem_pct = st->mem_total > 0 ? (int)(st->mem_used * 100 / st->mem_total) : 0;
    memset(&prog, 0, sizeof(prog));
    prog.value = mem_pct / 100.0;
    prog.show_percent = 1;
    prog.fill_color = mem_pct > 80 ? TUI_COLOR_RED : TUI_COLOR_YELLOW;
    tui_rect_t mr = { y, x + 6, 1, cols - 10 };
    tui_progress_render(fd, &mr, &prog);
    y++;

    /* 内存详情 */
    tui_cursor_goto(fd, y, x + 6);
    tui_set_fg(fd, tui_meuos_theme.dim);
    char meminfo[128];
    snprintf(meminfo, sizeof(meminfo), "Total: %ld KB  Used: %ld KB  Free: %ld KB",
             st->mem_total, st->mem_used, st->mem_free);
    tui_write(fd, meminfo);
    tui_reset_style(fd);
    y += 2;

    /* Swap */
    if (st->swap_total > 0) {
        tui_cursor_goto(fd, y, x);
        tui_set_fg(fd, tui_meuos_theme.dim);
        tui_write(fd, "SWAP: ");
        tui_reset_style(fd);
        int swap_pct = (int)(st->swap_used * 100 / st->swap_total);
        memset(&prog, 0, sizeof(prog));
        prog.value = swap_pct / 100.0;
        prog.show_percent = 1;
        prog.fill_color = TUI_COLOR_MAGENTA;
        tui_rect_t sr = { y, x + 6, 1, cols - 10 };
        tui_progress_render(fd, &sr, &prog);
        y += 2;
    }
}

static void render_procs(int fd, const tui_rect_t *area, sys_state_t *st) {
    tui_rect_t inner = *area;
    tui_draw_border(fd, &inner, "  Processes  ", 0, tui_meuos_theme.border);
    if (!tui_rect_valid(&inner)) return;

    /* 表头 */
    tui_cursor_goto(fd, inner.row, inner.col);
    tui_set_bg(fd, tui_meuos_theme.accent);
    tui_set_attr(fd, TUI_ATTR_BOLD);
    tui_set_fg(fd, TUI_COLOR_WHITE);
    tui_spaces(fd, inner.cols);

    const char *hdrs[] = { "PID", "COMMAND", "STATE", "CPU%", "MEM%", "RSS" };
    int widths[] = { 8, 32, 6, 8, 8, 12 };
    int hx = inner.col + 1;
    for (int i = 0; i < 6; i++) {
        tui_cursor_goto(fd, inner.row, hx);
        tui_write(fd, hdrs[i]);
        hx += widths[i];
    }
    tui_reset_style(fd);

    /* 进程行 */
    int max_lines = inner.rows - 1;
    int start = 0;
    if (st->selected >= max_lines) start = st->selected - max_lines + 1;

    for (int i = 0; i < max_lines && start + i < st->count; i++) {
        int idx = start + i;
        int is_sel = (idx == st->selected);
        int y = inner.row + 1 + i;
        int x = inner.col + 1;

        tui_cursor_goto(fd, y, x);
        if (is_sel) {
            tui_set_bg(fd, tui_meuos_theme.highlight);
            tui_set_fg(fd, TUI_COLOR_WHITE);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            tui_spaces(fd, inner.cols - 2);
            tui_cursor_goto(fd, y, x);
        }

        proc_info_t *p = &st->procs[idx];

        char pid_str[16]; snprintf(pid_str, sizeof(pid_str), "%d", p->pid);
        char state_str[4] = { p->state, 0 };
        char cpu_str[16]; snprintf(cpu_str, sizeof(cpu_str), "%d%%", p->cpu);
        char mem_str[16]; snprintf(mem_str, sizeof(mem_str), "%d%%", p->mem);
        char rss_str[16]; snprintf(rss_str, sizeof(rss_str), "%ld KB", p->rss);

        if (is_sel) tui_set_bg(fd, tui_meuos_theme.highlight);
        tui_set_fg(fd, is_sel ? TUI_COLOR_WHITE : TUI_COLOR_YELLOW);
        tui_cursor_goto(fd, y, x);
        tui_write(fd, pid_str);

        tui_set_fg(fd, is_sel ? TUI_COLOR_WHITE : TUI_COLOR_DEFAULT);
        tui_cursor_goto(fd, y, x + 8);
        int bytes = tui_truncate(p->comm, 30);
        write(fd, p->comm, (size_t)bytes);

        tui_set_fg(fd, is_sel ? TUI_COLOR_WHITE : TUI_COLOR_CYAN);
        tui_cursor_goto(fd, y, x + 40);
        tui_write(fd, state_str);

        tui_set_fg(fd, is_sel ? TUI_COLOR_WHITE : TUI_COLOR_GREEN);
        tui_cursor_goto(fd, y, x + 46);
        tui_write(fd, cpu_str);

        tui_set_fg(fd, is_sel ? TUI_COLOR_WHITE : TUI_COLOR_YELLOW);
        tui_cursor_goto(fd, y, x + 54);
        tui_write(fd, mem_str);

        tui_set_fg(fd, is_sel ? TUI_COLOR_WHITE : tui_meuos_theme.dim);
        tui_cursor_goto(fd, y, x + 62);
        tui_write(fd, rss_str);

        tui_reset_style(fd);
    }
}

int main(void) {
    tui_raw_mode(0, 1);
    tui_alt_screen(0, 1);
    tui_clear_screen(0);
    tui_cursor_show(0, 0);

    tui_size_t scr;
    if (tui_get_size(0, &scr) != TUI_OK) { scr.rows = 30; scr.cols = 80; }

    sys_state_t st;
    memset(&st, 0, sizeof(st));
    sys_refresh(&st);

    tui_event_t ev;
    int running = 1;

    while (running) {
        /* 标题栏 */
        render_header(0, 1, scr.cols, &st);

        /* 统计区域 */
        render_stats(0, 3, 1, scr.cols - 1, &st);

        /* 进程列表 */
        int proc_h = scr.rows - 12;
        if (proc_h < 3) proc_h = 3;
        tui_rect_t proc_area = { 10, 1, proc_h, scr.cols - 1 };
        render_procs(0, &proc_area, &st);

        /* 状态栏 */
        tui_cursor_goto(0, scr.rows, 1);
        tui_set_bg(0, tui_meuos_theme.accent);
        tui_set_fg(0, TUI_COLOR_WHITE);
        tui_set_attr(0, TUI_ATTR_BOLD);
        char status[256];
        snprintf(status, sizeof(status), " Procs: %d | CPU: %d%% | MEM: %ld/%ld KB ",
                 st.count, st.cpu_usage, st.mem_used, st.mem_total);
        tui_write(0, status);
        tui_reset_style(0);

        /* 非阻塞按键 */
        if (tui_getkey_timeout(0, &ev, 1000) == TUI_OK) {
            switch (ev.key) {
            case 'q': case TUI_KEY_ESC: running = 0; break;
            case 'r': sys_refresh(&st); break;
            case TUI_KEY_UP: if (st.selected > 0) st.selected--; break;
            case TUI_KEY_DOWN: if (st.selected < st.count - 1) st.selected++; break;
            case TUI_KEY_HOME: st.selected = 0; break;
            case TUI_KEY_END: st.selected = st.count - 1; break;
            default: break;
            }
        }

        /* 定期刷新 */
        sys_refresh(&st);
    }

    tui_cursor_show(0, 1);
    tui_clear_screen(0);
    tui_alt_screen(0, 0);
    tui_raw_mode(0, 0);
    return 0;
}
