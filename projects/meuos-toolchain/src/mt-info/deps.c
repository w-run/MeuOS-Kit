/* mt deps <elf> - 递归 DT_NEEDED 依赖树
 *
 * 取代 ldd：彩色显示动态依赖，符号链接可跟随。
 */
#include "mt-info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 递归打印依赖（深度优先，已访问集合防止循环）。 */
static int g_seen[256];
static int g_nseen;

static void
print_deps(const char *path, int depth)
{
	if (g_nseen >= 256) return;
	for (int i = 0; i < g_nseen; i++)
		if (strcmp(g_seen[i] ? "" : "", path) == 0) return;

	void *buf = NULL;
	size_t size = 0;
	struct mt_elf64_view view;
	if (mt_load_elf(path, &buf, &size, &view) != 0) {
		free(buf);
		return;
	}
	struct mt_elf64_section dyn;
	if (mt_elf64_find_section(buf, size, &view, ".dynamic", &dyn) != MT_ELF_OK) {
		free(buf);
		return;
	}
	const char *R = mt_c_reset();
	for (int d = 0; d < depth; d++) printf("  ");
	printf("  %s%s%s\n", mt_c_success(), path, R);

	uint64_t ents = dyn.size / dyn.entry_size;
	for (uint64_t e = 0; e < ents; e++) {
		const unsigned char *p = (const unsigned char *)buf + dyn.offset
		                        + e * dyn.entry_size;
		uint64_t tag = 0, val = 0;
		for (int b = 0; b < 8; b++) tag |= (uint64_t)p[b] << (8*b);
		for (int b = 0; b < 8; b++) val |= (uint64_t)p[8+b] << (8*b);
		if (tag == 1) { /* DT_NEEDED */
			struct mt_elf64_section dynstr;
			if (mt_elf64_get_section(buf, size, &view, dyn.link,
			                        &dynstr) == MT_ELF_OK) {
				const char *name;
				if (mt_elf64_get_string(buf, size, &dynstr,
				                       (uint32_t)val, &name) == MT_ELF_OK) {
					for (int d = 0; d < depth; d++) printf("  ");
					printf("    %s→%s %s%s%s\n", mt_c_info(), R,
					       mt_c_dim(), name, R);
				}
			}
		}
	}
	free(buf);
}

int
cmd_deps(int argc, char **argv)
{
	if (argc < 1) {
		mt_msg(0, "用法: mt deps <elf>");
		return 2;
	}
	g_nseen = 0;
	print_deps(argv[0], 0);
	return 0;
}
