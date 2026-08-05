/* meuos_exc runtime foundation gate.
 *
 * Emulates the m++-lowered shape by hand (the front-end try/catch landing
 * lands on the mcc side); validates the helper itself: register -> throw ->
 * longjmp -> catch reads the persisted typecode/value. */
#include <meuos_exc.h>
#include <stdio.h>

/* A single protected region.  `body` runs after register; if it throws,
 * control resumes here via longjmp with r!=0 and the caught values are
 * checked. */
static int
protect(int (*body)(int, unsigned long long),
	int tc_throw, unsigned long long val)
{
	_meuos_exc_frame frame;
	int r;

	r = setjmp(frame.env);          /* recover point for the catch */
	_meuos_exc_try_begin(&frame);   /* register the handler */
	if (r == 0) {
		int br = body(tc_throw, val);     /* may throw, or return normally */
		_meuos_exc_try_end();             /* normal completion: pop */
		return br;                        /* body did not throw */
	}
	/* longjmp returned to us: a throw was raised. */
	if (_meuos_exc_caught_type() != tc_throw)
		return 1;
	if (_meuos_exc_caught_value() != val)
		return 2;
	return 0;                       /* caught the right payload */
}

static int
thrower(int tc, unsigned long long val)
{
	_meuos_exc_throw(tc, val);      /* never returns on x86_64 setjmp reach */
	/* not reached */
	return -1;
}

static int
safe(int tc, unsigned long long val)
{
	(void)tc;
	(void)val;
	return 0;                       /* normal path: no throw */
}

int
main(void)
{
	/* 1) throw inside the protected region -> caught with matching payload. */
	if (protect(thrower, 42, 0xABCDEFull) != 0)
		return 10;
	/* 2) a large / signed-ish 64-bit value round-trips. */
	if (protect(thrower, -3, 0xFFFFFFFFFFFFFF00ull) != 0)
		return 11;
	/* 3) normal completion (no throw) must pop the handler cleanly. */
	if (protect(safe, 0, 0) != 0)
		return 12;
	puts("PASS exc");
	return 0;
}
