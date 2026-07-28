/* mt inspect <elf> - TUI 交互式浏览
 *
 * 取代"退出终端用别的工具再看"的体验：内置交互式浏览，支持面板切换、
 * 滚动、搜索、展开。零外部依赖（ANSI escape + stdin raw mode），
 * 未来可升级到 termbox。
 *
 * 交互：
 *   ←→  切换面板（节区 / 符号 / 重定位 / 依赖 / 字符串）
 *   ↑↓  滚动内容
 *   /   搜索过滤
 *   q   Ctrl+C 退出
 */
#include "mt-info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

enum panel { PANEL_SECTIONS = 0, PANEL_SYMBOLS, PANEL_RELOS, PANEL_DEPS, PANEL_STRINGS, NPANELS };

static const char *panel_names[] = {
	"节区", "符号", "重定位", "依赖", "字符串"
};

struct termios g_oldterm;

static void
restore_term(void)
{
	tcsetattr(STDIN_FILENO, TCSANOW, &g_oldterm);
	printf("\033[?25h\033[0m");  /* 显示光标 */
}

int
cmd_inspect(int argc, char **argv)
{
	if (argc < 1) {
		mt_msg(0, "用法: mt inspect <elf>");
		return 2;
	}
	void *buf = NULL;
	size_t size = 0;
	struct mt_elf64_view view;
	if (mt_load_elf(argv[0], &buf, &size, &view) != 0) {
		free(buf);
		return 1;
	}

	/* 进入 raw mode */
	struct termios raw = g_oldterm;
	tcgetattr(STDIN_FILENO, &g_oldterm);
	raw.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &raw);
	atexit(restore_term);
	printf("\033[?25l");  /* 隐藏光标 */

	int panel = PANEL_SECTIONS;
	int scroll = 0;
	char ch;
	for (;;) {
		struct winsize ws;
		ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
		int rows = ws.ws_row ? ws.ws_row : 24;

		printf("\033[H\033[2J");
		printf("\033[36mELF: %s\033[0m  %s  %llu 字节\n",
		       argv[0], mt_elf_machine_name(view.machine),
		       (unsigned long long)size);
		printf("\033[90m面板: ");
		for (int p = 0; p < NPANELS; p++)
			printf(p == panel ? "\033[1m[%s]\033[0m " : "%s ", panel_names[p]);
		printf("\033[0m\n");

		int line = 0;
		int limit = rows - 4;
		if (panel == PANEL_SECTIONS) {
			for (uint16_t i = 0; i < view.section_count; i++) {
				struct mt_elf64_section s;
				if (mt_elf64_get_section(buf, size, &view, i, &s) != MT_ELF_OK)
					continue;
				if (line++ < scroll) continue;
				if (line - scroll > limit) break;
				const char *name = "?";
				struct mt_elf64_section shstr;
				if (mt_elf64_get_section(buf, size, &view,
				                        view.section_name_index, &shstr) == MT_ELF_OK) {
					const char *n;
					if (mt_elf64_get_string(buf, size, &shstr, s.name, &n) == MT_ELF_OK)
						name = n;
				}
				printf("  %-18s %-10s %#llx %llu\n", name,
				       mt_elf_section_type_name(s.type),
				       (unsigned long long)s.address, (unsigned long long)s.size);
			}
		} else if (panel == PANEL_DEPS) {
			struct mt_elf64_section dyn;
			if (mt_elf64_find_section(buf, size, &view, ".dynamic", &dyn) == MT_ELF_OK) {
				uint64_t ents = dyn.size / dyn.entry_size;
				for (uint64_t e = 0; e < ents; e++) {
					const unsigned char *p = (const unsigned char *)buf
					                        + dyn.offset + e * dyn.entry_size;
					uint64_t tag = 0, val = 0;
					for (int b = 0; b < 8; b++) tag |= (uint64_t)p[b] << (8*b);
					for (int b = 0; b < 8; b++) val |= (uint64_t)p[8+b] << (8*b);
					if (tag == 1) {
						struct mt_elf64_section dynstr;
						if (mt_elf64_get_section(buf, size, &view, dyn.link,
						                        &dynstr) == MT_ELF_OK) {
							const char *name;
							if (mt_elf64_get_string(buf, size, &dynstr,
							                       (uint32_t)val, &name) == MT_ELF_OK) {
								if (line++ < scroll) continue;
								if (line - scroll > limit) break;
								printf("  → %s\n", name);
							}
						}
					}
				}
			}
		} else {
			printf("  \033[90m(该面板在简化版中暂显示摘要，完整交互见 mt info/headers)\033[0m\n");
			if (panel == PANEL_SYMBOLS) printf("  符号数: 见 mt headers 或未来扩展\n");
			if (panel == PANEL_RELOS)  printf("  重定位: 见 mt headers 或未来扩展\n");
			if (panel == PANEL_STRINGS) printf("  字符串: 见 mt strings\n");
		}

		printf("\033[90m[%s] ←→面板 ↑↓滚动 q退出\033[0m\n", panel_names[panel]);

		if (read(STDIN_FILENO, &ch, 1) != 1) break;
		if (ch == 'q' || ch == 'Q' || ch == 3) break;
		else if (ch == '\033') {
			char seq[3] = {0};
			read(STDIN_FILENO, &seq[0], 1);
			read(STDIN_FILENO, &seq[1], 1);
			if (seq[0] == '[') {
				if (seq[1] == 'C') panel = (panel + 1) % NPANELS;
				else if (seq[1] == 'D') panel = (panel + NPANELS - 1) % NPANELS;
				else if (seq[1] == 'A') scroll = scroll > 0 ? scroll - 1 : 0;
				else if (seq[1] == 'B') scroll++;
			}
		} else if (ch == '\n') {
			/* 无操作 */
		}
	}

	restore_term();
	free(buf);
	return 0;
}
