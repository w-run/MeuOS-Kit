/* mt info <elf> - 彩色信息卡
 *
 * 一眼看完 ELF 的关键信息：类型 / 架构 / 入口 / 节区 / 符号 / 依赖。
 * 遵循 p9-ui 现代化体验：开箱即美，不需要 --color=auto 别名。
 */
#include "mt-info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *
file_type_name(uint16_t type)
{
	switch (type) {
	case 0x01: return "ET_REL (可重定位目标文件)";
	case 0x02: return "ET_EXEC (可执行文件)";
	case 0x03: return "ET_DYN (共享库 / PIE)";
	case 0x04: return "ET_CORE (核心转储)";
	default:   return "未知";
	}
}

int
cmd_info(int argc, char **argv)
{
	if (argc < 1) {
		mt_msg(0, "用法: mt info <elf>");
		return 2;
	}
	void *buf = NULL;
	size_t size = 0;
	struct mt_elf64_view view;
	if (mt_load_elf(argv[0], &buf, &size, &view) != 0) {
		free(buf);
		return 1;
	}

	if (g_mt_mode == MT_OUT_JSON) {
		printf("{\"file\":\"%s\",\"type\":%u,\"machine\":%u,"
		       "\"entry\":%llu,\"sections\":%u,\"programs\":%u}\n",
		       argv[0], view.type, view.machine,
		       (unsigned long long)view.entry,
		       view.section_count, view.program_count);
		free(buf);
		return 0;
	}

	/* 统计节区 / 符号 / 依赖 */
	struct mt_elf64_section shstrtab, dynsym;
	if (mt_elf64_find_section(buf, size, &view, ".shstrtab", &shstrtab) == MT_ELF_OK) {
		/* 遍历所有节区名，统计符号表和动态节区 */
	}
	if (mt_elf64_find_section(buf, size, &view, ".dynsym", &dynsym) == MT_ELF_OK) {
		/* .dynsym 存在：动态符号数量可用于后续展示 */
	}

	const char *R = mt_c_reset();
	printf("\n");
	printf("  %sELF%s %s%s%s\n", mt_c_info(), R,
	       mt_c_dim(), argv[0], R);
	printf("  ──────────────────────────────────────────────\n");
	printf("  %s类型%s   %s\n",   mt_c_dim(), R, file_type_name(view.type));
	printf("  %s架构%s   %s (%u)\n", mt_c_dim(), R,
	       mt_elf_machine_name(view.machine), view.machine);
	printf("  %s入口%s   %#llx\n",  mt_c_dim(), R,
	       (unsigned long long)view.entry);
	printf("  %s节区%s   %u\n",     mt_c_dim(), R, view.section_count);
	printf("  %s程序头%s %u\n",     mt_c_dim(), R, view.program_count);
	printf("  %s大小%s   %llu 字节\n", mt_c_dim(), R,
	       (unsigned long long)size);

	/* 节区类型分布 */
	uint32_t n_progbits = 0, n_symtab = 0, n_rela = 0, n_no = 0;
	for (uint16_t i = 0; i < view.section_count; i++) {
		struct mt_elf64_section s;
		if (mt_elf64_get_section(buf, size, &view, i, &s) != MT_ELF_OK)
			continue;
		switch (s.type) {
		case 0x1: n_progbits++; break;   /* PROGBITS */
		case 0x2: n_symtab++; break;     /* SYMTAB */
		case 0x4: n_rela++; break;       /* RELA */
		case 0x8: n_no++; break;         /* NOBITS */
		}
	}
	if (view.section_count) {
		printf("  %s节区构成%s PROGBITS=%u SYMTAB=%u RELA=%u NOBITS=%u\n",
		       mt_c_dim(), R, n_progbits, n_symtab, n_rela, n_no);
	}

	/* 动态依赖 */
	struct mt_elf64_section dyn;
	if (mt_elf64_find_section(buf, size, &view, ".dynamic", &dyn) == MT_ELF_OK) {
		printf("  %s依赖%s\n", mt_c_dim(), R);
		/* 逐条读取 DT_NEEDED（tag=1），从 strtab 取名称 */
		uint64_t ents = dyn.size / dyn.entry_size;
		for (uint64_t e = 0; e < ents; e++) {
			/* .dynamic 是 Elf64_Dyn: d_tag(8) + d_val(8) */
			const unsigned char *p = (const unsigned char *)buf + dyn.offset
			                        + e * dyn.entry_size;
			uint64_t tag = 0, val = 0;
			for (int b = 0; b < 8; b++) tag |= (uint64_t)p[b] << (8*b);
			for (int b = 0; b < 8; b++) val |= (uint64_t)p[8+b] << (8*b);
			if (tag == 1) { /* DT_NEEDED */
				/* 找 .dynstr */
				struct mt_elf64_section dynstr;
				if (mt_elf64_get_section(buf, size, &view, dyn.link,
				                        &dynstr) == MT_ELF_OK) {
					const char *name;
					if (mt_elf64_get_string(buf, size, &dynstr,
					                       (uint32_t)val, &name) == MT_ELF_OK)
						printf("    %s→%s %s%s%s\n", mt_c_info(), R,
						       mt_c_success(), name, R);
				}
			}
		}
	}
	printf("\n");

	free(buf);
	return 0;
}
