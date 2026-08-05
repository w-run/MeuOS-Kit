/* unnamed_param_boundary.cc — unnamed-parameter boundary cases (m++).
 *
 * Builds on unnamed_param.cc (defect M regression: a single unnamed ctor
 * parameter).  Adds:
 *  - multiple unnamed parameters in a ctor and in a free function
 *  - a ctor mixing named and unnamed parameters (the unnamed ones only
 *    participate in mangling; the named ones are still referencable)
 *  - a member function with an unnamed parameter
 *  - overload resolution between two ctors distinguished only by the
 *    arity/types of their (unnamed) parameter lists
 *
 * NOTE: ctor arity is capped at 2 by m++ (a 3-parameter ctor call fails
 * to resolve — see u3 probe / .issues/0802).  Keep the ctor cases at
 * two parameters.
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
class Two {
public:
    Two(int, double) { v = 7; }
    Two(int) { v = 13; }
    int v;
};

class Mixed {
public:
    Mixed(int a, double, int b) { v = a + b; }
    int v;
};

int
multi(int, double)
{
    return 3;
}

int
mixed_args(int a, double, char c)
{
    return a + (int)c;
}

class M {
public:
    int mem(int) { return 9; }
    int mem2(int, double) { return 13; }
};

int
main(void)
{
    /* two-arg ctor with two unnamed params */
    Two a(1, 2.0);
    if (a.v != 7) return 1;

    /* overload: same class, single unnamed int */
    Two b(5);
    if (b.v != 13) return 2;

    /* mixed named + unnamed ctor params */
    Mixed m(40, 1.0, 2);
    if (m.v != 42) return 3;

    /* free function with two unnamed params */
    if (multi(1, 2.0) != 3) return 4;

    /* free function mixing named + unnamed */
    if (mixed_args(40, 1.0, 'b') != 40 + 'b') return 5;

    /* member functions with unnamed params */
    M mm;
    if (mm.mem(1) != 9) return 6;
    if (mm.mem2(1, 2.0) != 13) return 7;

    return 0;
}