/* mt headers <elf> - 快速 ELF / 节区头查看
 *
 * 取代 readelf -h / -S 的 80% 用途：一眼看清文件头和节区布局。
 */
#include "mt-info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
cmd_headers(int argc, char **argv)
{
	if (argc < 1) {
		mt_msg(0, "用法: mt headers <elf>");
		return 2;
	}
	void *buf = NULL;
	size_t size = 0;
	struct mt_elf64_view view;
	if (mt_load_elf(argv[0], &buf, &size, &view) != 0) {
		free(buf);
		return 1;
	}
	const char *R = mt_c_reset();

	if (g_mt_mode == MT_OUT_JSON) {
		printf("{\"file\":\"%s\",\"type\":%u,\"machine\":%u,"
		       "\"entry\":%llu,\"sections\":[", argv[0], view.type,
		       view.machine, (unsigned long long)view.entry);
		for (uint16_t i = 0; i < view.section_count; i++) {
			struct mt_elf64_section s;
			if (mt_elf64_get_section(buf, size, &view, i, &s) != MT_ELF_OK)
				continue;
			if (i) printf(",");
			printf("{\"index\":%u,\"type\":%u,\"addr\":%llu,\"size\":%llu}",
			       i, s.type, (unsigned long long)s.address,
			       (unsigned long long)s.size);
		}
		printf("]}\n");
		free(buf);
		return 0;
	}

	printf("\n  %sELF 文件头%s\n", mt_c_info(), R);
	printf("  类型    : %s\n", mt_elf_machine_name(view.machine));
	printf("  入口    : %#llx\n", (unsigned long long)view.entry);
	printf("  节区数  : %u\n", view.section_count);
	printf("  程序头  : %u\n", view.program_count);
	printf("\n  %s节区头%s\n", mt_c_info(), R);
	fputs(mt_c_dim(), stdout);
	printf("  %-4s %-18s %-10s %-10s %-10s\n",
	       "Idx", "名称", "类型", "地址", "大小");
	fputs(R, stdout);
	for (uint16_t i = 0; i < view.section_count; i++) {
		struct mt_elf64_section s;
		if (mt_elf64_get_section(buf, size, &view, i, &s) != MT_ELF_OK)
			continue;
		/* 节区名从 .shstrtab 读取 */
		const char *name = "(null)";
		struct mt_elf64_section shstr;
		if (mt_elf64_get_section(buf, size, &view,
		                        view.section_name_index, &shstr) == MT_ELF_OK) {
			const char *n;
			if (mt_elf64_get_string(buf, size, &shstr, s.name, &n) == MT_ELF_OK)
				name = n;
		}
		printf("  %-4u %-18s %-10s %#-9llx %llu\n", i, name,
		       mt_elf_section_type_name(s.type),
		       (unsigned long long)s.address, (unsigned long long)s.size);
	}
	printf("\n");
	free(buf);
	return 0;
}
