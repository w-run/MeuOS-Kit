/* C23 typeof / typeof_unqual (§6.7.3)。
 *
 * 覆盖点：
 *   - typeof(expr)：与表达式类型一致（保留 const/volatile）；
 *   - typeof_unqual(expr)：去掉顶层 const/volatile（含复合限定）；
 *   - typeof(typename)：对数组类型得到数组类型本身（指针+解引用沿用）；
 *   - typeof 在变量声明、指针声明中的用法。
 */
extern int puts(const char *);

int main(void) {
	/* 基础：与变量类型一致 */
	int x = 3;
	typeof(x) y = x;
	if (y != 3) {
		puts("FAIL: typeof var");
		return 1;
	}

	/* typeof 保留限定符：const 变量取 typeof 后赋值应被拒（编译期）；
	 * 此处只验证可读路径 */
	const int cx = 5;
	typeof(cx) cy = cx;
	if (cy != 5) {
		puts("FAIL: typeof const");
		return 2;
	}

	/* typeof_unqual：剥掉 const volatile */
	const volatile int vx = 7;
	typeof_unqual(vx) vy = vx;
	int *p = &vy;          /* 无限定才能赋给 int* */
	*p = 8;
	if (*p != 8) {
		puts("FAIL: typeof_unqual");
		return 3;
	}

	/* 数组：typeof(a) 仍是数组类型，指向它的指针即数组指针 */
	int a[5];
	typeof(a) *pa = &a;
	(*pa)[2] = 9;
	if (a[2] != 9) {
		puts("FAIL: typeof array");
		return 4;
	}

	/* typeof(表达式) 也可用于表达式 */
	int r = (typeof(x))0 + 1;
	if (r != 1) {
		puts("FAIL: typeof expr");
		return 5;
	}

	puts("PASS");
	return 0;
}
