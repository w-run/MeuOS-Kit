/* aggregate_arg.c — structs passed by value / returned (SysV ABI, m++).
 *
 * Exercises the by-value aggregate argument/return path on both the LIR
 * and MIR-native backends:
 *  - 8-byte struct passed in one GPR (two ints pack into RDI/RSI slots)
 *  - 16-byte struct passed in two GPRs
 *  - >16-byte struct passed on the stack (with BLIT copy)
 *  - struct return in RAX:RDX (<=16B) and via hidden sret pointer (>16B)
 *  - mixed scalar + aggregate argument lists (register-class accounting)
 *
 * Each check returns a distinct exit code; run via `check-c-mir`.
 */
extern int puts(const char *);

struct Pair { int x; int y; };        /* 8 bytes  -> one GPR */
struct Quad { int a; int b; int c; int d; };  /* 16 bytes -> two GPRs */
struct Big  { int v[8]; };            /* 32 bytes -> stack */

int
pair_sum(struct Pair p)
{
	return p.x + p.y;
}

int
quad_sum(struct Quad q)
{
	return q.a + q.b + q.c + q.d;
}

int
big_sum(struct Big b)
{
	int s = 0, i;
	for (i = 0; i < 8; i++)
		s += b.v[i];
	return s;
}

struct Pair
make_pair(int a, int b)
{
	struct Pair p;
	p.x = a;
	p.y = b;
	return p;
}

struct Big
make_big(int base)
{
	struct Big b;
	int i;
	for (i = 0; i < 8; i++)
		b.v[i] = base + i;
	return b;
}

/* register-class accounting: one aggregate GPR arg then an SSE arg */
double
mix(int a, struct Pair p, double d)
{
	return (double)(a + p.x + p.y) + d;
}

int
main(void)
{
	struct Pair p = {3, 4};
	struct Quad q = {1, 2, 3, 4};
	struct Big b;
	int i, expect = 0;

	if (pair_sum(p) != 7) return 1;
	if (quad_sum(q) != 10) return 2;

	for (i = 0; i < 8; i++) {
		b.v[i] = i;
		expect += i;
	}
	if (big_sum(b) != expect) return 3;

	/* struct returns */
	p = make_pair(10, 20);
	if (p.x != 10 || p.y != 20) return 4;
	b = make_big(5);
	if (b.v[0] != 5 || b.v[7] != 12) return 5;

	/* mixed aggregate + FP */
	if (mix(1, p, 0.5) != 31.5) return 6;

	puts("PASS");
	return 0;
}
