/* grading.c — -O 优化级别运行时正确性回归（check-olevel）。
 *
 * 各 -O 级别（-O0/-O1/-O2/-O3/-Os/-Oz/-Og）编译本文件运行都必须
 * 返回 0；任何级别结果错或编译失败都判定为回归。
 *
 * 覆盖模式（与 run.sh 的 asm 差异检查互补，这里只校验正确性）：
 *   - redun_branch: 分支选择（-O2 可 if 转换，-O1 保留分支）
 *   - mul8:         乘 2 次幂（-O3/-Os/-Oz 强度削减为移位）
 *   - leaf_add:     叶函数（-Og 保留帧指针）
 *   - const_arith:  常量折叠/复制传播
 */
static int
redun_branch(int x, int y)
{
	int r;
	if (x < y)
		r = x;
	else
		r = y;
	return r;
}

static int
mul8(int x)
{
	return x * 8;
}

static int
leaf_add(int x)
{
	return x + 1;
}

static int
const_arith(int x)
{
	int k = 7;         /* 复制传播后可折叠 k+1 */
	return x + (k + 1);
}

/* --- cmov / if-conversion 模式（-O2+ 生成条件移动，-O1 保留分支）--- */
static int
cmov_max(int a, int b)
{
	return a > b ? a : b;   /* 三元，INTEGER 选择 */
}

static int
cmov_abs(int x)
{
	return x < 0 ? -x : x;  /* 三元，一侧为表达式 */
}

static unsigned
cmov_umax(unsigned a, unsigned b)
{
	return a > b ? a : b;   /* 无符号比较（unsigned） */
}

static long
cmov_lmin(long a, long b)
{
	return a < b ? a : b;   /* 64 位选择 */
}

static int
cmov_bool(int c, int x, int y)
{
	return c ? x : y;       /* 直接布尔条件（无比较 setcc） */
}

int
main(void)
{
	int s = 0;

	s += redun_branch(3, 9);   /* 3 */
	s += redun_branch(9, 3);   /* 3 */
	s += mul8(5);              /* 40 */
	s += leaf_add(7);          /* 8 */
	s += const_arith(0);       /* 8 */
	s += cmov_max(5, 9);       /* 9 */
	s += cmov_max(9, 5);       /* 9 */
	s += cmov_abs(-4);         /* 4 */
	s += cmov_abs(4);          /* 4 */
	s += (int)cmov_umax(3u, 9u);  /* 9 */
	s += (int)cmov_umax(9u, 3u);  /* 9 */
	s += (int)cmov_lmin(5L, 9L);  /* 5 */
	s += (int)cmov_lmin(9L, 5L);  /* 5 */
	s += cmov_bool(1, 7, 3);   /* 7 */
	s += cmov_bool(0, 7, 3);   /* 3 */
	if (s != 126)
		return 1;
	return 0;
}
