/* C99: for 循环作用域声明 + 块内混合声明/语句 (§6.8.5.3, §6.8.2)。
 *
 * C99 起：
 *   - for 初始化子句可直接声明变量（`for (int i = ...)`），其作用域
 *     为循环体，循环结束后不可见；
 *   - 块内允许声明与语句交错（不再要求声明全部前置）。
 */
extern int puts(const char *);

int main(void) {
	int after = 0;
	int t = 0;

	/* for 声明：i 只存活于循环 */
	for (int i = 0; i < 4; ++i) {
		int j = i * 2;       /* 混合声明：位于语句之后 */
		t += j;
	}
	if (t != 12) {
		puts("FAIL: for-decl");
		return 1;
	}

	/* 循环后同名变量可重新声明 */
	for (int i = 10; i < 12; ++i)
		after += i;
	if (after != 21) {
		puts("FAIL: redecl after loop");
		return 2;
	}

	/* 声明/语句交错 + 局部遮蔽 */
	int x = 1;
	x += 1;
	int y = x * 10;
	y += x;
	if (y != 22) {
		puts("FAIL: mixed decl/stmt");
		return 3;
	}

	puts("PASS");
	return 0;
}
