/* sizez.c — -Oz 尺寸优先回归（check-olevel）。
 *
 * -Oz 比 -Os 更激进：常量 0 / 32 位小常量用 movl（5 字节）替代
 * movq（7 字节）。本文件以 0 与小常量为主，-Oz 的 .text 必须
 * ≤ -Os 的 .text。运行时两级别结果一致且返回 0。
 */
static int
zero_add(int x){ return x + 0; }

static long
zero64(void){ return 0; }

static int
small_c(int x){ return x + 5; }

static long
big_c(void){ return 1000000L; }

static long
neg_c(void){ return -1L; }

int
main(void)
{
	/* 1 + 0 + 7 + 1000000 + (-1) = 1000007 */
	int s = zero_add(1) + (int)zero64() + small_c(2) +
	        (int)big_c() + (int)neg_c();
	return s == 1000007 ? 0 : 1;
}
