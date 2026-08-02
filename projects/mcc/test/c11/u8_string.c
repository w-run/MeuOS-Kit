/* C11 u8 字符串字面量 (§6.4.5)。
 *
 * 覆盖点（当前 mcc 可用子集）：
 *   - `u8"..."` 初始化 char 数组：内容按 UTF-8 单字节编码逐字节填入；
 *   - 相邻 u8 字面量拼接（stringconcat），sizeof 含末尾 NUL；
 *   - 元素值访问（`u8"x"[0]`）。
 *
 * 已知缺陷（见 .issues/0802.md 缺陷 W）：mcc 把 u8 字面量元素类型置为
 * unsigned char（C11 规定应为 plain char），导致 `char *s = u8"x"` /
 * `const char *s = u8"x"` 被拒（需强转）。此处只覆盖能通过的数组初始化路径。
 */
extern int puts(const char *);

int main(void) {
	/* 数组初始化：u8 字面量按字节展开 */
	char s[] = u8"ab";
	if (sizeof(s) != 3 || s[0] != 0x61 || s[1] != 0x62 || s[2] != 0) {
		puts("FAIL: u8 char[] init");
		return 1;
	}

	/* 相邻 u8 拼接 */
	char t[] = u8"ab" u8"cd";
	if (sizeof(t) != 5 || t[0] != 0x61 || t[3] != 0x64 || t[4] != 0) {
		puts("FAIL: u8 concat");
		return 2;
	}

	/* 下标访问 */
	if (u8"x"[0] != 0x78) {
		puts("FAIL: u8 subscript");
		return 3;
	}

	/* 混排前缀不允许（应编译期报错，此处验证正例之外不误报） */
	puts("PASS");
	return 0;
}
