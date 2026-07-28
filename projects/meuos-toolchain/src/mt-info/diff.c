/* mt diff <a.elf> <b.elf> - 语义 diff
 *
 * 取代 `objdump -d a > /tmp/a; diff /tmp/a /tmp/b` 的笨办法：
 * 直接对比节区大小变化、符号增减、重定位差异，彩色标注。
 */
#include "mt-info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct snap {
	char *buf;
	size_t size;
	struct mt_elf64_view view;
};

static int
load_snap(const char *path, struct snap *s)
{
	if (mt_load_elf(path, (void **)&s->buf, &s->size, &s->view) != 0)
		return -1;
	return 0;
}

int
cmd_diff(int argc, char **argv)
{
	if (argc < 2) {
		mt_msg(0, "用法: mt diff <a.elf> <b.elf>");
		return 2;
	}
	struct snap a, b;
	if (load_snap(argv[0], &a) != 0) { free(a.buf); return 1; }
	if (load_snap(argv[1], &b) != 0) { free(a.buf); free(b.buf); return 1; }
	const char *R = mt_c_reset();

	printf("%s对比%s %s%s%s ↔ %s%s%s\n", mt_c_dim(), R,
	       mt_c_info(), argv[0], R, mt_c_info(), argv[1], R);

	/* 节区大小对比 */
	printf("\n  %s节区大小变化%s\n", mt_c_info(), R);
	for (uint16_t i = 0; i < a.view.section_count; i++) {
		struct mt_elf64_section sa;
		if (mt_elf64_get_section(a.buf, a.size, &a.view, i, &sa) != MT_ELF_OK)
			continue;
		/* 按名匹配 */
		struct mt_elf64_section shstr;
		const char *name = "?";
		if (mt_elf64_get_section(a.buf, a.size, &a.view,
		                        a.view.section_name_index, &shstr) == MT_ELF_OK) {
			const char *n;
			if (mt_elf64_get_string(a.buf, a.size, &shstr, sa.name, &n) == MT_ELF_OK)
				name = n;
		}
		for (uint16_t j = 0; j < b.view.section_count; j++) {
			struct mt_elf64_section sb;
			if (mt_elf64_get_section(b.buf, b.size, &b.view, j, &sb) != MT_ELF_OK)
				continue;
			struct mt_elf64_section shstrb;
			const char *nameb = "?";
			if (mt_elf64_get_section(b.buf, b.size, &b.view,
			                        b.view.section_name_index, &shstrb) == MT_ELF_OK) {
				const char *nb;
				if (mt_elf64_get_string(b.buf, b.size, &shstrb, sb.name, &nb)
				    == MT_ELF_OK)
					nameb = nb;
			}
			if (strcmp(name, nameb) != 0) continue;
			if (sa.size != sb.size) {
				long long delta = (long long)sb.size - (long long)sa.size;
				const char *col = delta > 0 ? mt_c_warn() : mt_c_success();
				printf("    %s%s%s: %llu → %llu (%s%lld%s)\n", mt_c_dim(), name, R,
				       (unsigned long long)sa.size, (unsigned long long)sb.size,
				       col, delta, R);
			}
			break;
		}
	}

	/* 文件大小 */
	long long dsize = (long long)b.size - (long long)a.size;
	printf("\n  %s总大小%s %llu → %llu (%s%lld%s)\n", mt_c_dim(), R,
	       (unsigned long long)a.size, (unsigned long long)b.size,
	       dsize > 0 ? mt_c_warn() : mt_c_success(), dsize, R);

	free(a.buf);
	free(b.buf);
	return 0;
}
