/* memconst.c — -O1 内存局部常量传播回归（check-olevel）。
 *
 * 模式：局部变量初始化为常量且未被修改（int k = 7），后续读取 k 的 load
 * 应被 MIR FOLD 的内存常量传播折叠为常量 7（进而 k+1 折叠为 8）。-O1 应
 * 比 -O0 生成更少的汇编指令。
 *
 * 覆盖：
 *   - const_local: 基本折叠（k=7，k+1 → 8）
 *   - const_twice: 多个常量局部（互不干扰）
 *   - overwrite:   k 被重新赋值后不得折叠（k 最终为 9，结果仍须正确）
 */
static int
const_local(int x)
{
	int k = 7;
	return x + (k + 1);
}

static int
const_twice(int x)
{
	int a = 3;
	int b = 5;
	return x * a + b;
}

static int
overwrite(int x)
{
	int k = 7;
	k = 9;
	return k + x;
}

int
main(void)
{
	if (const_local(0) != 8) return 1;
	if (const_local(10) != 18) return 2;
	if (const_twice(4) != 17) return 3;
	if (overwrite(1) != 10) return 4;
	return 0;
}
