/* branch.c — -O1 与 -O2 的分支/if 转换差异检查（check-olevel）。
 * 无 main：仅用于 -S 汇编，run.sh 检查 -O2 出 cmov、-O1 保留分支。
 */
int
sel(int x, int y)
{
	int r;
	if (x < y)
		r = x;
	else
		r = y;
	return r;
}
