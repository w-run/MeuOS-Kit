/* tls_basic.c — _Thread_local globals on both backends (SysV x86-64).
 *
 * Exercises the TLS access sequences a compiler must emit:
 *  - read/write a TLS int (direct %fs:...@tpoff memory operand)
 *  - a function bumping a TLS counter (load, modify, store)
 *  - read/write a TLS double (floating-point TLS storage)
 *  - taking the ADDRESS of a TLS variable (fs_base + tpoff) in one
 *    function and dereferencing it in another
 *
 * Runs single-threaded (no pthread) so it is a pure codegen check.
 * Each check returns a distinct exit code; run via `check-c-mir`.
 */
extern int puts(const char *);

static _Thread_local int counter;
static _Thread_local double fval;

int
bump(int by)
{
	counter += by;
	return counter;
}

double *
fval_ptr(void)
{
	return &fval;
}

int *
counter_ptr(void)
{
	return &counter;
}

int
main(void)
{
	int *p;

	counter = 5;
	if (counter != 5) return 1;
	if (bump(3) != 8) return 2;
	if (bump(-2) != 6) return 3;

	fval = 2.5;
	if (fval != 2.5) return 4;
	*fval_ptr() = 3.75;
	if (fval != 3.75) return 5;

	/* address-of TLS from a separate function, dereferenced here */
	p = counter_ptr();
	*p = 42;
	if (counter != 42) return 6;

	puts("PASS");
	return 0;
}
