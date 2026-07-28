/* complex/complex.c — Complex number support for mcc (non-GCC path) */

#include <complex.h>

double
creal(double complex z)
{
	return __real__(z);
}

double
cimag(double complex z)
{
	return __imag__(z);
}

double complex
conj(double complex z)
{
	return __real__(z) - __imag__(z) * I;
}

float
crealf(float complex z)
{
	return __real__(z);
}

float
cimagf(float complex z)
{
	return __imag__(z);
}

float complex
conjf(float complex z)
{
	return __real__(z) - __imag__(z) * I;
}
