/* aggregate_return.c — ≤16B struct return via SysV registers (MIR-native).
 *
 * Regression for the x86_64 MIR-native backend bug where a ≤16B
 * aggregate returned by value dereferenced the returned register data as
 * an address (mabi_selcall only implemented the >16B sret path).
 *
 * Covers:
 *  - 8B  struct {int,int}              -> RAX
 *  - 12B struct {int,int,int}          -> RAX + RDX
 *  - 16B struct {int x4}               -> RAX + RDX
 *  - 16B struct {double,double}        -> XMM0 + XMM1
 *  - 16B struct {double,int} (mixed)   -> XMM0 + RDX
 *  - 24B struct (sret / memory)        -> control comparison
 *  - a returned aggregate used directly as a by-value argument
 *
 * Each check returns a distinct exit code; run via `check-c-mir`.
 */
extern int puts(const char *);

struct S8  { int a, b; };
struct S12 { int a, b, c; };
struct S16 { int a, b, c, d; };
struct F16 { double x, y; };
struct M16 { double d; int i; };
struct S24 { int v[6]; };

struct S8
mk8(void)
{
	struct S8 s;
	s.a = 1;
	s.b = 2;
	return s;
}

struct S12
mk12(void)
{
	struct S12 s;
	s.a = 1; s.b = 2; s.c = 3;
	return s;
}

struct S16
mk16(void)
{
	struct S16 s;
	s.a = 1; s.b = 2; s.c = 3; s.d = 4;
	return s;
}

struct F16
mkf16(void)
{
	struct F16 s;
	s.x = 1.5;
	s.y = 2.5;
	return s;
}

struct M16
mkm16(void)
{
	struct M16 s;
	s.d = 3.5;
	s.i = 7;
	return s;
}

struct S24
mk24(void)
{
	struct S24 s;
	int i;
	for (i = 0; i < 6; i++)
		s.v[i] = i;
	return s;
}

int
sum8(struct S8 s)
{
	return s.a + s.b;
}

int
sum24(struct S24 s)
{
	int i, t = 0;
	for (i = 0; i < 6; i++)
		t += s.v[i];
	return t;
}

int
main(void)
{
	struct S8 s8 = mk8();
	if (s8.a != 1 || s8.b != 2) return 1;

	struct S12 s12 = mk12();
	if (s12.a != 1 || s12.b != 2 || s12.c != 3) return 2;

	struct S16 s16 = mk16();
	if (s16.a != 1 || s16.b != 2 || s16.c != 3 || s16.d != 4) return 3;

	struct F16 f16 = mkf16();
	if (f16.x != 1.5 || f16.y != 2.5) return 4;

	struct M16 m16 = mkm16();
	if (m16.d != 3.5 || m16.i != 7) return 5;

	struct S24 s24 = mk24();
	if (s24.v[0] != 0 || s24.v[5] != 5) return 6;

	/* returned aggregate consumed directly as a by-value argument */
	if (sum8(mk8()) != 3) return 7;
	if (sum24(mk24()) != 15) return 8;

	puts("PASS");
	return 0;
}
