/* leaf.c — -Og 帧指针差异检查（check-olevel）。
 * 无 main：仅用于 -S 汇编。run.sh 检查 -Og 下叶函数保留 pushq %rbp、
 * -O2 下省略（rsp 基址）。
 */
int
leaf_add(int x)
{
	return x + 1;
}
