#include <math.h>
#include <stddef.h>

/* Minimal soft math implementations.  These are not IEEE-754 compliant
 * but cover the common cases needed by real-world software builds. */

double
fabs(double x)
{
	return x < 0 ? -x : x;
}

float
fabsf(float x)
{
	return x < 0 ? -x : x;
}

double
floor(double x)
{
	long i = (long)x;

	return x < 0 && x != (double)i ? (double)(i - 1) : (double)i;
}

double
ceil(double x)
{
	long i = (long)x;

	return x > 0 && x != (double)i ? (double)(i + 1) : (double)i;
}

float
floorf(float x)
{
	return (float)floor((double)x);
}

float
ceilf(float x)
{
	return (float)ceil((double)x);
}

double
round(double x)
{
	return x >= 0 ? floor(x + 0.5) : ceil(x - 0.5);
}

double
trunc(double x)
{
	return x >= 0 ? floor(x) : ceil(x);
}

long
lround(double x)
{
	return (long)round(x);
}

double
fmod(double x, double y)
{
	if (y == 0)
		return NAN;
	return x - (long)(x / y) * y;
}

int
isnan(double x)
{
	return x != x;
}

int
isinf(double x)
{
	return x != 0 && x == x / 2;
}

int
isfinite(double x)
{
	return !isnan(x) && !isinf(x);
}

int
fpclassify(double x)
{
	if (isnan(x))
		return FP_NAN;
	if (isinf(x))
		return FP_INFINITE;
	if (x == 0)
		return FP_ZERO;
	return FP_NORMAL;
}

/* Stubs for functions that need libm — replaced with real implementations */
static double
m_abs(double x) { return x < 0 ? -x : x; }

double
sqrt(double x)
{
	if (x <= 0) return 0;
	if (x == 1.0) return 1.0;
	/* Simple Newton: x_{n+1} = (x_n + S/x_n) / 2 */
	double r = x > 1.0 ? x * 0.5 : x * 2.0;
	int i;
	for (i = 0; i < 30; i++) {
		double nv = (r + x / r) * 0.5;
		if (m_abs(nv - r) < 1e-15 * (m_abs(r) + 1e-15)) break;
		r = nv;
	}
	return r;
}

static double
m_ln2(void) { return 0.6931471805599453; }

double
log(double x)
{
	if (x <= 0) return -1e308 /* -inf */;
	/* Use identity: log(x * 2^n) = log(x) + n * ln2 */
	int e = 0;
	double m = x;
	while (m >= 2.0) { m *= 0.5; e++; }
	while (m < 1.0) { m *= 2.0; e--; }
	/* log(m) for m in [1,2) using series centered at 1 */
	double t = (m - 1.0) / (m + 1.0);
	double t2 = t * t;
	double s = t;
	double term = t;
	int n;
	for (n = 3; n < 200; n += 2) {
		term *= t2;
		double new_s = s + term / (double)n;
		if (new_s == s) break;
		s = new_s;
	}
	return 2.0 * s + (double)e * m_ln2();
}

double
log2(double x)
{
	return log(x) / m_ln2();
}

double
log10(double x)
{
	return log(x) / 2.302585092994046;
}

static double
m_exp_series(double x)
{
	/* exp(x) via Taylor series, x in [-1, 1] */
	double r = 1.0 + x;
	double term = x;
	int n;
	for (n = 2; n < 50; n++) {
		term *= x / (double)n;
		double nr = r + term;
		if (nr == r) break;
		r = nr;
	}
	return r;
}

double
exp(double x)
{
	if (x < -700) return 0;
	if (x > 700) return 1e308; /* inf */
	/* Reduce range using exp(x) = exp(x/2^n)^(2^n) */
	int n = 0;
	double xr = x;
	while (m_abs(xr) > 1.0) { xr *= 0.5; n++; }
	double r = m_exp_series(xr);
	while (n-- > 0) r *= r;
	return r;
}

double
pow(double b, double e)
{
	if (b == 0) {
		if (e == 0) return 1.0;		/* 0^0 = 1 */
		return e > 0 ? 0.0 : 1e308;	/* 0^neg = +inf */
	}
	if (b > 0)
		return exp(e * log(b));
	/* Negative base: only defined for integral exponents; the sign of the
	 * result follows the parity of the (truncated) exponent. */
	if (e >= -9007199254740992.0 && e <= 9007199254740992.0 &&
	    e == (e < 0 ? ceil(e) : floor(e))) {
		double r = exp(e * log(-b));
		return ((long long)e & 1) ? -r : r;
	}
	return NAN;
}

/* sin/cos via Taylor series with range reduction */
static double
m_pi(void) { return 3.141592653589793; }

static double
m_2pi(void) { return 6.283185307179586; }

static double
m_pi_2(void) { return 1.5707963267948966; }

static double
sin_series(double x)
{
	/* sin(x) = x - x^3/3! + x^5/5! - ... */
	double x2 = x * x;
	double r = x;
	double term = x;
	int n;
	for (n = 3; n < 60; n += 2) {
		term *= -x2 / (double)(n * (n - 1));
		double nr = r + term;
		if (nr == r) break;
		r = nr;
	}
	return r;
}

double
sin(double x)
{
	/* Reduce to [-pi, pi] */
	while (x > m_pi()) x -= m_2pi();
	while (x < -m_pi()) x += m_2pi();
	return sin_series(x);
}

double
cos(double x)
{
	/* cos(x) = sin(x + pi/2) */
	return sin(x + m_pi_2());
}

double
tan(double x)
{
	double s = sin(x);
	double c = cos(x);
	if (c == 0) return 0;
	return s / c;
}

/* atan on |z| <= 1 via a 5th-order minimax on z^2 (~1e-7 absolute). */
static double
atan_small(double z)
{
	double z2 = z * z;
	return z * (0.9998660 + z2 * (-0.3302995 +
	       z2 * (0.1801410 + z2 * (-0.0851330 + z2 * 0.0208351))));
}

double
atan(double x)
{
	if (x != x) return x;          /* NaN */
	if (x > 1.0)
		return m_pi_2() - atan_small(1.0 / x);
	if (x < -1.0)
		return -m_pi_2() - atan_small(1.0 / x);
	return atan_small(x);
}

double
atan2(double y, double x)
{
	double a;
	if (x > 0)
		a = atan(y / x);
	else if (x < 0 && y >= 0)
		a = atan(y / x) + m_pi();
	else if (x < 0 && y < 0)
		a = atan(y / x) - m_pi();
	else if (y > 0)                 /* x == 0 */
		a = m_pi_2();
	else if (y < 0)
		a = -m_pi_2();
	else
		a = 0.0;                    /* atan2(0,0) -> 0 (and errno EDOM) */
	return a;
}

double
asin(double x)
{
	if (x > 1.0)  x = 1.0;         /* clamp (domain error for |x|>1) */
	if (x < -1.0) x = -1.0;
	if (x == 1.0) return m_pi_2();
	if (x == -1.0) return -m_pi_2();
	return atan(x / sqrt((1.0 - x) * (1.0 + x)));
}

double
acos(double x)
{
	/* acos(x) = pi/2 - asin(x); asin range [-pi/2,pi/2] forces acos in [0,pi] */
	return m_pi_2() - asin(x);
}

float sqrtf(float x) { return (float)sqrt((double)x); }

double
ldexp(double x, int exp)
{
	/* Simple ldexp: multiply by 2^exp via integer loop */
	double r = x;

	if (exp > 0)
		while (exp--) r *= 2.0;
	else
		while (exp++) r *= 0.5;
	return r;
}

float
ldexpf(float x, int exp)
{
	return (float)ldexp((double)x, exp);
}

double
frexp(double x, int *exp)
{
	if (x == 0) { *exp = 0; return 0; }
	int e = 0;
	double r = x;
	if (r < 0) r = -r;
	while (r >= 1.0) { r *= 0.5; e++; }
	while (r < 0.5) { r *= 2.0; e--; }
	*exp = e;
	return x < 0 ? -r : r;
}

double
modf(double x, double *iptr)
{
	long i = (long)x;

	*iptr = (double)i;
	return x - (double)i;
}
