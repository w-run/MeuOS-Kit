/* aggregate_ret_small.c — ≤16B aggregate by-value returns (SysV x86-64).
 *
 * Regression coverage for the MIR-native backend's aggregate-return path:
 *  - 8B struct  -> one INTEGER eightbyte in RAX
 *  - 12B struct -> two INTEGER eightbytes in RAX:RDX
 *  - 16B struct -> two INTEGER eightbytes in RAX:RDX
 *  - 16B all-SSE struct -> XMM0:XMM1
 *  - 16B MIXED {INTEGER,SSE} struct -> RAX + XMM0  (per-class SysV
 *    register sequence; a positional slot1->XMM1/RDX map breaks this)
 *  - nested use of the returned value
 *
 * Each check returns a distinct exit code; run via `check-c-mir`.
 */
extern int puts(const char *);

struct P8 { int a; int b; };
struct P12 { int a; int b; int c; };
struct P16 { int a; int b; int c; int d; };
struct F16 { double x; double y; };
struct M16 { int i; double d; };

struct P8
mk8(void)
{
	struct P8 p;
	p.a = 3;
	p.b = 4;
	return p;
}

struct P12
mk12(void)
{
	struct P12 p;
	p.a = 1;
	p.b = 2;
	p.c = 3;
	return p;
}

struct P16
mk16(void)
{
	struct P16 p;
	p.a = 5;
	p.b = 6;
	p.c = 7;
	p.d = 8;
	return p;
}

struct F16
mkf(void)
{
	struct F16 p;
	p.x = 1.5;
	p.y = 2.5;
	return p;
}

struct M16
mkm(void)
{
	struct M16 p;
	p.i = 9;
	p.d = 0.25;
	return p;
}

int
main(void)
{
	struct P8 p8 = mk8();
	if (p8.a != 3) return 1;
	if (p8.b != 4) return 2;

	struct P12 p12 = mk12();
	if (p12.a != 1 || p12.b != 2 || p12.c != 3) return 3;

	struct P16 p16 = mk16();
	if (p16.a != 5 || p16.b != 6 || p16.c != 7 || p16.d != 8) return 4;

	struct F16 f16 = mkf();
	if (f16.x != 1.5 || f16.y != 2.5) return 5;

	/* mixed INTEGER + SSE eightbytes: regressed to wrong value (and
	 * pre-fix, SIGSEGV from the uninitialized return pad) */
	struct M16 m16 = mkm();
	if (m16.i != 9) return 6;
	if (m16.d != 0.25) return 7;

	/* nested access on the returned value */
	if (mk8().a != 3) return 8;

	puts("PASS");
	return 0;
}
