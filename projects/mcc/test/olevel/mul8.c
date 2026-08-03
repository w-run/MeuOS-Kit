/* mul8.c — -O2 与 -O3/-Os/-Oz 的强度削减差异检查（check-olevel）。
 * 无 main：仅用于 -S 汇编，run.sh 检查 -O2 出 imul、-O3/-Os/-Oz 出 shl。
 */
int
mul8(int x)
{
	return x * 8;
}
