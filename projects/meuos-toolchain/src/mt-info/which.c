/* mt which <symbol> - 搜索符号：哪个库定义了该符号？
 *
 * 取代 `nm -o *.a | grep` 的猜谜游戏：给定符号名，从默认搜索路径
 * （/usr/lib + MEUOS_SYSROOT/usr/lib）的 .a/.so 中定位定义者。
 */
#include "mt-info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

static const char *g_search_dirs[8];
static int g_ndirs;

static void
collect_dir(const char *dir)
{
	if (g_ndirs >= 8) return;
	DIR *d = opendir(dir);
	if (!d) return;
	g_search_dirs[g_ndirs++] = dir;
	closedir(d);
}

/* 在一个 ELF 文件中查找导出的符号（dynsym 或 symtab 中 DEFINED 且全局）。 */
static void
scan_file(const char *path, const char *sym)
{
	FILE *f = fopen(path, "rb");
	if (!f) return;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz <= 0 || sz > 64*1024*1024) { fclose(f); return; }
	void *buf = malloc(sz);
	if (!buf) { fclose(f); return; }
	if (fread(buf, 1, sz, f) != (size_t)sz) { free(buf); fclose(f); return; }
	fclose(f);

	struct mt_elf64_view view;
	if (mt_elf64_parse(buf, sz, &view) != MT_ELF_OK) { free(buf); return; }

	/* 遍历 symtab + dynsym */
	for (int tbl = 0; tbl < 2; tbl++) {
		const char *secname = tbl == 0 ? ".symtab" : ".dynsym";
		struct mt_elf64_section table;
		if (mt_elf64_find_section(buf, sz, &view, secname, &table) != MT_ELF_OK)
			continue;
		struct mt_elf64_section strtab;
		if (mt_elf64_get_section(buf, sz, &view, table.link, &strtab) != MT_ELF_OK)
			continue;
		uint64_t nsym = table.size / table.entry_size;
		for (uint64_t k = 0; k < nsym; k++) {
			struct mt_elf64_symbol symbol;
			if (mt_elf64_get_symbol(buf, sz, &table, k, &symbol) != MT_ELF_OK)
				continue;
			const char *name;
			if (mt_elf64_get_string(buf, sz, &strtab, symbol.name, &name) != MT_ELF_OK)
				continue;
			if (strcmp(name, sym) != 0) continue;
			uint8_t bind = MT_ELF64_ST_BIND(symbol.info);
			uint8_t typ = MT_ELF64_ST_TYPE(symbol.info);
			/* 定义者：非 UNDEF 且类型为 FUNC/OBJECT */
			if (symbol.section != 0 && (typ == 0x2 || typ == 0x1)) {
				const char *R = mt_c_reset();
				printf("  %s✔%s %s%s%s  (%s, %s)\n", mt_c_success(), R,
				       mt_c_info(), path, R,
				       bind == 0x1 ? "global" : "local",
				       typ == 0x2 ? "func" : "object");
			}
		}
	}
	free(buf);
}

static void
scan_dir(const char *dir, const char *sym)
{
	DIR *d = opendir(dir);
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		size_t len = strlen(e->d_name);
		if (len < 2) continue;
		const char *ext = e->d_name + len - 2;
		if (strcmp(ext, ".a") != 0 && strcmp(ext, ".o") != 0 &&
		    strcmp(ext, ".so") != 0)
			continue;
		char full[4096];
		snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
		scan_file(full, sym);
	}
	closedir(d);
}

int
cmd_which(int argc, char **argv)
{
	if (argc < 1) {
		mt_msg(0, "用法: mt which <symbol>");
		return 2;
	}
	const char *sym = argv[0];
	g_ndirs = 0;
	const char *sysroot = getenv("MEUOS_SYSROOT");
	char path[4096];
	if (sysroot) {
		snprintf(path, sizeof(path), "%s/usr/lib", sysroot);
		collect_dir(path);
	}
	collect_dir("/usr/lib");
	collect_dir("/lib");

	const char *R = mt_c_reset();
	printf("%s搜索符号%s %s%s%s\n", mt_c_dim(), R, mt_c_info(), sym, R);
	for (int i = 0; i < g_ndirs; i++)
		scan_dir(g_search_dirs[i], sym);
	return 0;
}
