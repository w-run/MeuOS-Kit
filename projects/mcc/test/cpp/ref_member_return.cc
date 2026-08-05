/* ref_member_return.cc — reference-returning member methods (m++).
 *
 * Covers:
 *  - `int &at()` member method: write-through (`v.at() = x`), read
 *  - `int &&at()` rvalue-reference returning member method
 *  - a const-qualified reference-returning method
 *  - coexistence of a reference-returning method and a reference-
 *    returning operator[] on the same class
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
struct V {
    int x;
    int &at() { return x; }
};

struct W {
    int x;
    int &&at() { return x; }
};

struct C {
    int x;
    const int &at() const { return x; }
};

struct Mix {
    int x;
    int &at() { return x; }
    int &operator[](int) { return x; }
};

int
main(void)
{
    /* 1. reference-returning member method writes through */
    {
        V v; v.x = 1;
        v.at() = 5;
        if (v.x != 5) return 1;
    }
    /* 2. reading through the returned reference */
    {
        V v; v.x = 7;
        int s = v.at() + v.at();
        if (s != 14) return 2;
    }
    /* 3. && reference-returning member method */
    {
        W w; w.x = 9;
        if (w.at() != 9) return 3;
    }
    /* 4. const-qualified reference-returning member method */
    {
        C c; c.x = 3;
        if (c.at() != 3) return 4;
        const C cc = {4};
        if (cc.at() != 4) return 4;
    }
    /* 5. reference-returning method coexists with operator[] */
    {
        Mix m; m.x = 1;
        m.at() = 5;
        m[0] = 9;
        if (m.x != 9) return 5;
    }
    return 0;
}
