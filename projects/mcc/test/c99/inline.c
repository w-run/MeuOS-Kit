/* C99 inline functions (§6.7.4).
 *
 * 覆盖点：
 *   - static inline 定义：TU 内直接内联展开，无需外部符号；
 *   - 多个 static inline 互相调用；
 *   - inline 仅作提示，static inline 函数地址可被取用（退化到普通符号）。
 *
 * 注意：C99/C11 6.7.4p7 要求「inline 定义 + 后续 extern 声明」应产生
 * 外部定义（`inline int f(...) {} extern int f(...);`），mcc 当前会
 * 链接失败 —— 已登记为缺陷 X（见 .issues/0802.md），此处不覆盖。
 */
extern int puts(const char *);

static inline int sq(int x) {
	return x * x;
}

static inline int add(int a, int b) {
	return a + b;
}

static inline int (*get_sq(void))(int) {
	return sq;   /* 取 static inline 函数地址 */
}

int main(void) {
	if (sq(3) != 9) {
		puts("FAIL: sq");
		return 1;
	}
	if (add(2, 5) != 7) {
		puts("FAIL: add");
		return 2;
	}
	/* 嵌套 static inline 调用 */
	if (sq(add(1, 2)) != 9) {
		puts("FAIL: nested inline");
		return 3;
	}
	/* 通过函数指针间接调用 static inline */
	int (*fp)(int) = get_sq();
	if (fp(4) != 16) {
		puts("FAIL: inline fn-ptr");
		return 4;
	}
	puts("PASS");
	return 0;
}
