/* test/targets/fp.c — MIR-native floating-point coverage sample.
 *
 * Exercises FP arithmetic, comparisons, conversions and constants on the
 * riscv64/aarch64 MIR-native backends (fpfill.sh assembles this with the
 * target GNU as).  Scalar integer/float only — aggregates/varargs/TLS/VLA
 * still fall back to the legacy LIR backend.
 */
double fadd(double a, double b) { return a + b; }
double fsubd(double a, double b) { return a - b; }
float fmulf(float a, float b) { return a * b; }
double fdivd(double a, double b) { return a / b; }
float fdivf(float a, float b) { return a / b; }
double fneg(double a) { return -a; }
float fnegf(float a) { return -a; }

/* FP comparison -> boolean (SETCCR) and use in a branch */
int fcmp(double a, double b) { return a < b ? 1 : 0; }
int fcmpu(unsigned long a, double b) { return a >= b; }

/* int<->fp conversions */
double i2d(int n) { return (double)n; }
float i2f(long n) { return (float)n; }
double ui2d(unsigned int n) { return (double)n; }
int d2i(double d) { return (int)d; }
long f2l(float f) { return (long)f; }

/* widening / narrowing */
double widen(float f) { return (double)f; }
float narrow(double d) { return (float)d; }

/* FP constants and a global */
double gv = 3.5;
double getg(void) { return gv; }
double consts(void) { return 1.5 * 2.0 + 0.25; }

/* FP passed/returned across calls */
double calladd(double a, double b) { return fadd(a, b) + fsubd(a, b); }
