/* C11 u8 字符串字面量 (§6.4.5)。
 *
 * 覆盖点（当前 mcc 可用子集）：
 *   - `u8"..."` 初始化 char 数组：内容按 UTF-8 单字节编码逐字节填入；
 *   - 相邻 u8 字面量拼接（stringconcat），sizeof 含末尾 NUL；
 *   - 元素值访问（`u8"x"[0]`）。
 *
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

	/* 元素类型：C11 §6.4.5p6 规定 u8 字面量元素类型为 char，
	 * 因此可隐式初始化 char* / const char*（缺陷 c-00 回归守卫） */
	{
		char *s = u8"x";
		const char *cs = u8"y";
		if (s[0] != 0x78 || cs[0] != 0x79) {
			puts("FAIL: u8 element type is char");
			return 4;
		}
	}

	/* 混排前缀不允许（应编译期报错，此处验证正例之外不误报） */
	puts("PASS");
	return 0;
}
