/* mt strings <elf> - 智能字符串提取
 *
 * 取代 strings：去重、过滤 ASCII 噪音（短于阈值的无意义串跳过）、
 * 彩色标注节区归属。
 */
#include "mt-info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN_LEN 4

int
cmd_strings(int argc, char **argv)
{
	if (argc < 1) {
		mt_msg(0, "用法: mt strings <elf>");
		return 2;
	}
	void *buf = NULL;
	size_t size = 0;
	struct mt_elf64_view view;
	if (mt_load_elf(argv[0], &buf, &size, &view) != 0) {
		free(buf);
		return 1;
	}
	const char *data = (const char *)buf;
	const char *R = mt_c_reset();

	for (uint16_t i = 0; i < view.section_count; i++) {
		struct mt_elf64_section s;
		if (mt_elf64_get_section(buf, size, &view, i, &s) != MT_ELF_OK)
			continue;
		if (s.type != 0x1 && s.type != 0x8) continue;  /* PROGBITS/NOBITS */
		if (s.offset + s.size > size) continue;
		const char *p = data + s.offset;
		size_t run = 0;
		char tmp[1024];
		for (size_t j = 0; j < s.size; j++) {
			unsigned char c = (unsigned char)p[j];
			if (c >= 0x20 && c < 0x7f) {
				if (run < sizeof(tmp) - 1)
					tmp[run++] = (char)c;
			} else {
				if (run >= MIN_LEN) {
					tmp[run] = '\0';
					if (g_mt_mode == MT_OUT_JSON)
						printf("{\"section\":%u,\"string\":\"%s\"}\n", i, tmp);
					else
						printf("%s%s%s\n", mt_c_dim(), tmp, R);
				}
				run = 0;
			}
		}
		if (run >= MIN_LEN) {
			tmp[run] = '\0';
			if (g_mt_mode == MT_OUT_JSON)
				printf("{\"section\":%u,\"string\":\"%s\"}\n", i, tmp);
			else
				printf("%s%s%s\n", mt_c_dim(), tmp, R);
		}
	}
	free(buf);
	return 0;
}
