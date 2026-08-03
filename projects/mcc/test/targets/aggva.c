/* test/targets/aggva.c — MIR-native aarch64 full-coverage sample.
 *
 * Exercises aggregate passing/return (≤16B register chunks and >16B sret),
 * AAPCS64 varargs, TLS and VLA on the aarch64 MIR-native backend.  The
 * fpfill.sh script assembles this with aarch64-linux-gnu-as.
 */
#include <stdarg.h>

/* ≤16B aggregate: 8-byte register chunks */
struct P { int x, y; };
struct P mkp(int x, int y) { struct P p; p.x = x; p.y = y; return p; }
int px(struct P p) { return p.x + p.y; }

/* all-FP aggregate: v0/v1 chunks */
struct FP { double a, b; };
struct FP mkfp(double a, double b) { struct FP f; f.a = a; f.b = b; return f; }
double fpx(struct FP f) { return f.a + f.b; }

/* >16B aggregate: sret through x8 */
struct Big { long a, b, c, d; };
struct Big mkbig(long v) { struct Big b; b.a = b.b = b.c = b.d = v; return b; }
long sumbig(struct Big b) { return b.a + b.b + b.c + b.d; }

/* AAPCS64 varargs: GP, FP and mixed */
int vsum(int n, ...) {
	va_list ap; va_start(ap, n);
	int s = 0;
	for (int i = 0; i < n; i++) s += va_arg(ap, int);
	va_end(ap);
	return s;
}
double vfsum(int n, ...) {
	va_list ap; va_start(ap, n);
	double s = 0;
	for (int i = 0; i < n; i++) s += va_arg(ap, double);
	va_end(ap);
	return s;
}
int vmix(int n, ...) {
	va_list ap; va_start(ap, n);
	int a = va_arg(ap, int);
	double d = va_arg(ap, double);
	int b = va_arg(ap, int);
	va_end(ap);
	return a + (int)d + b;
}

/* TLS */
static __thread int tls_var = 42;
int get_tls(void) { return tls_var; }
void set_tls(int v) { tls_var = v; }

/* VLA */
int vla_sum(int n) {
	int a[n];
	int s = 0;
	for (int i = 0; i < n; i++) { a[i] = i + 1; s += a[i]; }
	return s;
}
