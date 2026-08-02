/* C99 inline functions (§6.7.4).
 *
 * 覆盖点：
 *   - static inline 定义：TU 内直接内联展开，无需外部符号；
 *   - 多个 static inline 互相调用；
 *   - inline 仅作提示，static inline 函数地址可被取用（退化到普通符号）；
 *   - C99/C11 6.7.4p6「inline 定义 + 后续 extern 声明」产生外部定义
 *     （缺陷 c-01 已修复）：`inline int f(...) {} extern int f(...);`
 *     使 f 成为外部定义，同 TU 内调用必须链接成功；
 *   - extern 声明在前的同一形式（本就应产生外部定义）。
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

/* C99/C11 6.7.4p6: inline 定义 + 后续 extern 声明 → 外部定义（缺陷 c-01）。
 * 修复前 `extern int exported_inline(int);` 不触发外部定义发射，链接
 * 时产生 undefined reference；修复后同 TU 内调用可正常链接。 */
inline int exported_inline(int x) {
	return x * 100;
}
extern int exported_inline(int);

/* extern 声明在前 + inline 定义在后（本就应产生外部定义，回归保护） */
extern int pre_extern(int);
inline int pre_extern(int x) {
	return x - 7;
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
	/* inline 定义 + 后续 extern 声明：产生外部定义（缺陷 c-01） */
	if (exported_inline(5) != 500) {
		puts("FAIL: inline+extern");
		return 5;
	}
	/* extern 声明在前 + inline 定义在后：产生外部定义 */
	if (pre_extern(20) != 13) {
		puts("FAIL: extern+inline");
		return 6;
	}
	puts("PASS");
	return 0;
}
