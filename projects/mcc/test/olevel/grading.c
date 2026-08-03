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

int
main(void)
{
	int s = 0;

	s += redun_branch(3, 9);   /* 3 */
	s += redun_branch(9, 3);   /* 3 */
	s += mul8(5);              /* 40 */
	s += leaf_add(7);          /* 8 */
	s += const_arith(0);       /* 8 */
	if (s != 62)
		return 1;
	return 0;
}
