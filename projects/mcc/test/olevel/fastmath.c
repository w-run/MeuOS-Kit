/* fastmath.c — -Ofast fast-math 折叠回归（check-olevel）。
 *
 * 各代数恒等式在 -Ofast（g_fast_math）下应被折叠（x*1.0→x 等，
 * 假设无 NaN/Inf/符号零语义）；-O3（无 fast-math）应保留浮点运算。
 * 非 NaN 场景下折叠结果与 IEEE 语义一致，运行时必须返回 0。
 */
static double
fm_mul1(double x){ return x * 1.0; }

static double
fm_add0(double x){ return x + 0.0; }

static double
fm_sub0(double x){ return x - 0.0; }

static double
fm_mul0(double x){ return x * 0.0; }

static double
fm_div1(double x){ return x / 1.0; }

static double
fm_selfsub(double x){ return x - x; }

static double
fm_selfdiv(double x){ return x / x; }

static double
fm_0add(double x){ return 0.0 + x; }

static double
fm_1mul(double x){ return 1.0 * x; }

int
main(void)
{
	/* 非 NaN 场景：折叠后结果与 IEEE 语义一致 */
	if (fm_mul1(3.5) != 3.5) return 1;
	if (fm_add0(1.5) != 1.5) return 1;
	if (fm_sub0(0.5) != 0.5) return 1;
	if (fm_mul0(2.0) != 0.0) return 1;
	if (fm_div1(4.0) != 4.0) return 1;
	if (fm_selfsub(7.0) != 0.0) return 1;
	if (fm_selfdiv(9.0) != 1.0) return 1;
	if (fm_0add(2.5) != 2.5) return 1;
	if (fm_1mul(6.0) != 6.0) return 1;
	return 0;
}
