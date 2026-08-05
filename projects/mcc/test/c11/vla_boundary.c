/* vla_boundary.c — dynamic alloca / variable-length arrays (MIR-native).
 *
 * Exercises the runtime stack-allocation path on both backends:
 *  - VLA sized from a function argument (runtime size, not constant)
 *  - VLA of a struct (element size > 8)
 *  - VLA re-allocated on every loop iteration
 *  - VLA then a call whose stack arguments (arg 7+) are placed below the
 *    lowered rsp (SALLOC + dynamic alloca interaction)
 *
 * Each check returns a distinct exit code; run via `check-c-mir`.
 */
extern int puts(const char *);

int
sum_vla(int n)
{
	int a[n];
	int s = 0, i;
	for (i = 0; i < n; i++)
		a[i] = i + 1;
	for (i = 0; i < n; i++)
		s += a[i];
	return s;
}

struct Pt { int x; int y; int z; };

int
vla_struct(int n)
{
	struct Pt pts[n];
	int s = 0, i;
	for (i = 0; i < n; i++) {
		pts[i].x = i;
		pts[i].y = i * 2;
		pts[i].z = i * 3;
	}
	for (i = 0; i < n; i++)
		s += pts[i].x + pts[i].y + pts[i].z;
	return s;
}

int
vla_in_loop(int n)
{
	int total = 0, k;
	for (k = 0; k < 3; k++) {
		int buf[n];
		int i;
		for (i = 0; i < n; i++)
			buf[i] = i;
		total += buf[n - 1];
	}
	return total;
}

int
add8(int a, int b, int c, int d, int e, int f, int g, int h)
{
	return a + b + c + d + e + f + g + h;
}

int
vla_call(int n)
{
	int a[n];
	int i;
	for (i = 0; i < n; i++)
		a[i] = i + 1;
	/* 8 args: args 7..8 go on the stack, BELOW the rsp the VLA lowered */
	return add8(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
}

int
main(void)
{
	if (sum_vla(10) != 55) return 1;
	if (sum_vla(1) != 1) return 2;
	if (vla_struct(5) != 60) return 3;   /* 6*(0+1+2+3+4) */
	if (vla_in_loop(4) != 9) return 4;   /* 3 * 3 */
	if (vla_call(8) != 36) return 5;     /* 1+..+8 */

	puts("PASS");
	return 0;
}
