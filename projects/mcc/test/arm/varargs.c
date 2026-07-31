/* arm vararg regression: M1 (stack double alignment) and M2
 * (64-bit Kl packing) — see projects/mcc/src/target/arm/arm_abi.c.
 *
 * Covers:
 *  - four named GPR words + int + double + long long varargs on the
 *    stack (the M1 alignment trap: the double lands at stack byte 4
 *    unless the caller 8-aligns the stack offset);
 *  - long long varargs > 32 bits both on the stack (M2) and in a
 *    register pair R2:R3;
 *  - float varargs and 4-byte varargs in registers.
 *
 * Each variadic callee returns 0 when all checks pass; main maps a
 * nonzero return to a distinct exit code. */
typedef __builtin_va_list va_list;

/* 4 named pointers fill R0-R3; varargs go on the caller stack. */
static long long
sum_ll_stack(char *a, char *b, char *c, char *d, ...)
{
	va_list ap;
	long long ll;
	int x;
	double dd;

	__builtin_va_start(ap, d);
	if (a[0] != 'a' || b[0] != 'b' || c[0] != 'c' || d[0] != 'd')
		return -1;
	x = __builtin_va_arg(ap, int);
	if (x != 12345)
		return -2;
	dd = __builtin_va_arg(ap, double);
	if (dd != 2.5)
		return -3;
	ll = __builtin_va_arg(ap, long long);
	if (ll != 0x123456789abcdef0LL)
		return -4;
	return 0;
}

/* Two named pointers (R0,R1); a long long vararg lands in R2:R3. */
static long long
sum_ll_reg(char *a, char *b, ...)
{
	va_list ap;
	long long ll;
	double dd;
	int x;

	__builtin_va_start(ap, b);
	ll = __builtin_va_arg(ap, long long);
	if (ll != 0x1122334455667788LL)
		return -11;
	dd = __builtin_va_arg(ap, double);
	if (dd != -1.25)
		return -12;
	x = __builtin_va_arg(ap, int);
	if (x != 777)
		return -13;
	if (a[0] != 'a' || b[0] != 'b')
		return -14;
	return 0;
}

/* One named pointer; a float vararg is promoted to double (C default
 * argument promotion) and lands in R2:R3; an int vararg in R1... */
static long long
sum_mixed(char *a, ...)
{
	va_list ap;
	double dd;
	int x;

	__builtin_va_start(ap, a);
	dd = __builtin_va_arg(ap, double);
	if (dd != 3.5)
		return -21;
	x = __builtin_va_arg(ap, int);
	if (x != 42)
		return -22;
	if (a[0] != 'a')
		return -23;
	return 0;
}

/* Named params of a variadic callee are packed per the base standard:
 * Kl (pointer/long/long long) counts ONE word — a deliberate mcc model
 * trade-off so pointers stay 1 word like GCC's ILP32 — so the named
 * long long below stays within 32 bits; the vararg that follows lands
 * on the stack as a full 64-bit value. */
static long long
sum_named_ll(char *a, char *b, long long n, ...)
{
	va_list ap;
	long long ll;

	__builtin_va_start(ap, n);
	if (n != 0xabcdabcdLL)
		return -31;
	ll = __builtin_va_arg(ap, long long);
	if (ll != 0x1111111122222222LL)
		return -32;
	if (a[0] != 'a' || b[0] != 'b')
		return -33;
	return 0;
}

int
main(void)
{
	long long r;

	r = sum_ll_stack("a", "b", "c", "d", 12345, 2.5, 0x123456789abcdef0LL);
	if (r != 0)
		return 1;

	r = sum_ll_reg("a", "b", 0x1122334455667788LL, -1.25, 777);
	if (r != 0)
		return 2;

	r = sum_mixed("a", 3.5f, 42);
	if (r != 0)
		return 3;

	r = sum_named_ll("a", "b", 0xabcdabcdLL, 0x1111111122222222LL);
	if (r != 0)
		return 4;

	return 0;
}
