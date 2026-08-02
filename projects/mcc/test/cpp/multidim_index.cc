/* multidim_index.cc — C++23 multidimensional / multi-argument operator[]
 * (P2128) and the underlying single-argument member subscript.
 *
 * Covers:
 *  - member `operator[]` with one argument (value capture)
 *  - reference-returning `operator[]` writes through (`a[i] = x`)
 *  - C++23 multi-argument `operator[](int, int)` invoked as `m[i, j]`
 *  - reference-returning multi-arg subscript (`m[i, j] = x`)
 *  - chained `a[i][j]` via a proxy type's operator[]
 *  - const member operator[] (const and non-const objects)
 *  - non-member `operator[]` (P2128R8)
 *  - builtin array subscript still works (no regression)
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
struct Vec {
    int data[3];
    int operator[](int i) { return data[i]; }
};

struct RefVec {
    int data[3];
    int &operator[](int i) { return data[i]; }
};

struct Matrix {
    int data[4];
    int operator[](int i, int j) { return data[i * 2 + j]; }
};

struct RefMatrix {
    int data[4];
    int &operator[](int i, int j) { return data[i * 2 + j]; }
};

struct Row {
    int data[2];
    int &operator[](int j) { return data[j]; }
};
struct Mat {
    Row rows[2];
    Row &operator[](int i) { return rows[i]; }
};

struct CVec {
    int data[3];
    int operator[](int i) const { return data[i]; }
};

struct Pair { int a, b; };
int operator[](Pair p, int i) { return i == 0 ? p.a : p.b; }

int
main(void)
{
    /* 1. single-argument member operator[], value capture */
    {
        Vec v; v.data[0] = 1; v.data[1] = 2; v.data[2] = 3;
        int s = v[0] + v[1] + v[2];
        if (s != 6) return 1;
    }
    /* 2. reference-returning operator[] writes through */
    {
        RefVec v; v.data[0] = 1; v.data[1] = 2; v.data[2] = 3;
        v[1] = 10;
        int s = v[0] + v[1] + v[2];
        if (s != 14 || v.data[1] != 10) return 2;
    }
    /* 3. multi-argument operator[](int, int), value capture */
    {
        Matrix m; m.data[0] = 1; m.data[1] = 2; m.data[2] = 3; m.data[3] = 4;
        int s = m[0, 0] + m[0, 1] + m[1, 0] + m[1, 1];
        if (s != 10) return 3;
    }
    /* 4. multi-argument reference-returning subscript writes through */
    {
        RefMatrix m; m.data[0] = 1; m.data[1] = 2; m.data[2] = 3; m.data[3] = 4;
        m[1, 1] = 40;
        if (m.data[3] != 40 || m[0, 1] != 2) return 4;
    }
    /* 5. chained a[i][j] through a proxy row type */
    {
        Mat m;
        m.rows[0].data[0] = 1; m.rows[0].data[1] = 2;
        m.rows[1].data[0] = 3; m.rows[1].data[1] = 4;
        m[1][0] = 30;
        int s = m[0][0] + m[0][1] + m[1][0] + m[1][1];
        if (s != 37 || m.rows[1].data[0] != 30) return 5;
    }
    /* 6. const member operator[] from a const object */
    {
        const CVec v = {{1, 2, 3}};
        if (v[0] + v[2] != 4) return 6;
    }
    /* 7. const member operator[] from a non-const object */
    {
        CVec v; v.data[0] = 4; v.data[1] = 5; v.data[2] = 6;
        if (v[1] != 5) return 7;
    }
    /* 8. non-member operator[] (P2128R8) */
    {
        Pair p; p.a = 5; p.b = 7;
        if (p[0] + p[1] != 12) return 8;
    }
    /* 9. subscript inside a helper function */
    {
        Vec v; v.data[0] = 1; v.data[1] = 2; v.data[2] = 3;
        int helper(Vec x, int i);
        if (helper(v, 2) != 3) return 9;
    }
    /* 10. builtin array subscript regression */
    {
        int arr[3] = {1, 2, 3};
        int s = 0;
        for (int i = 0; i < 3; ++i) s += arr[i];
        if (s != 6) return 10;
    }
    return 0;
}

int
helper(Vec x, int i)
{
    return x[i];
}
