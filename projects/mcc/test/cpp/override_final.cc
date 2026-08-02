/* override_final.cc — C++11 override/final specifiers (m++).
 *
 * `int f() override {}` and `virtual int g() final {}` are accepted and
 * consumed by the member-function path (struct_decl.c).  `override`
 * marks the member virtual (so it allocates a vtable slot and the
 * override is dispatched through the base's vtable); `final` is
 * recorded for the virtual-table checks.
 *
 * Covers: override of a base virtual with correct polymorphic dispatch
 * through a base reference, a non-overriding virtual with final, and a
 * plain virtual retained in the same class.
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
struct A {
	virtual int f() { return 1; }
	virtual int h() { return 10; }
};

struct B : A {
	int f() override { return 2; }
};

struct C : A {
	int f() override { return 3; }
	virtual int g() final { return 30; }
};

int
main(void)
{
	B b;
	C c;

	if (b.f() != 2) return 1;
	if (c.f() != 3) return 2;
	if (c.g() != 30) return 3;

	/* polymorphic dispatch through base references */
	A &r1 = b;
	A &r2 = c;
	if (r1.f() != 2) return 4;
	if (r2.f() != 3) return 5;

	/* a base virtual untouched by override still dispatches */
	if (r1.h() != 10) return 6;
	if (r2.h() != 10) return 7;

	/* direct calls still work after the specifiers */
	C c2;
	if (c2.g() != 30) return 8;

	return 0;
}
