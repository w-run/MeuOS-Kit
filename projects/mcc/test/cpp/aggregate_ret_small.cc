/* aggregate_ret_small.cc — ≤16B aggregate by-value returns (m++, C++).
 *
 * C++-side regression coverage for the MIR-native backend: a chained
 * operator+ whose temporary is re-passed by value, a 16B mixed
 * {INTEGER,SSE} struct return (the register-class bug), and a struct
 * returned then stored into a local.  Mirrors test/c11/aggregate_ret_small.c
 * for the C++ frontend (same shared backend).
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 */
extern int puts(const char *);

class Vec {
public:
	Vec(int v) { m = v; }
	Vec operator+(Vec o) { Vec r(m + o.m); return r; }
	int m;
};

struct M16 { int i; double d; };

M16
make_mixed(void)
{
	M16 p;
	p.i = 7;
	p.d = 0.5;
	return p;
}

int
main(void)
{
	/* chained operator+: the inner call's returned temporary is passed
	 * by value to the second operator+ */
	Vec a(1), b(2), c(3);
	Vec sum = a + b + c;
	if (sum.m != 6) { puts("FAIL: chained operator+"); return 1; }

	/* 16B mixed INTEGER+SSE struct return */
	M16 m = make_mixed();
	if (m.i != 7) { puts("FAIL: mixed struct int"); return 2; }
	if (m.d != 0.5) { puts("FAIL: mixed struct double"); return 3; }

	puts("PASS");
	return 0;
}
