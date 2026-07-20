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

/* Stubs for functions that need libm; real-world builds that actually
 * call these will need proper implementations. */
double sqrt(double x) { (void)x; return 0; }
double pow(double b, double e) { (void)b; (void)e; return 0; }
double log(double x) { (void)x; return 0; }
double log2(double x) { (void)x; return 0; }
double log10(double x) { (void)x; return 0; }
double exp(double x) { (void)x; return 0; }
double sin(double x) { (void)x; return 0; }
double cos(double x) { (void)x; return 0; }
double tan(double x) { (void)x; return 0; }
float sqrtf(float x) { (void)x; return 0; }

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
