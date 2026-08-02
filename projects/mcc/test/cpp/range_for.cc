/* range_for.cc — C++ range-based for loops (m++).
 *
 * Covers:
 *  - native arrays with `auto x` (value capture) and explicit types
 *  - C++20 init-statement: `for (int i = 0; auto x : arr)`
 *  - single-statement and compound bodies
 *  - reference element binding (`int &x`) writes through to the array
 *  - value-capture safety: modifying the copy does not touch the array
 *  - types with `begin()` / `end()` members (pointer iterators)
 *  - struct member arrays as the range
 *  - string literals (including the NUL element of char[N])
 *  - nested range-for loops
 *  - break / continue inside the body
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
struct Vec {
    int data[3];
    int *begin() { return &data[0]; }
    int *end() { return &data[3]; }
};
struct Holder { int data[3]; };

int
main(void)
{
    int arr[4] = {1, 2, 3, 4};

    /* array, `auto x` value capture */
    {
        int s = 0;
        for (auto x : arr) s += x;
        if (s != 10) return 1;
    }
    /* array, explicit element type */
    {
        int s = 0;
        for (int x : arr) s += x;
        if (s != 10) return 2;
    }
    /* C++20 init-statement */
    {
        int s = 0;
        for (int i = 0; auto x : arr) s += x + i;
        if (s != 10) return 3;   /* i is re-initialized to 0 each iteration */
    }
    /* single-statement body */
    {
        int s = 0;
        for (auto x : arr) s += x;
        if (s != 10) return 4;
    }
    /* reference element: writes through to the array */
    {
        int a[3] = {1, 2, 3};
        for (int &x : a) x += 10;
        if (a[0] != 11 || a[1] != 12 || a[2] != 13) return 5;
    }
    /* const reference element */
    {
        int s = 0;
        for (const int &x : arr) s += x;
        if (s != 10) return 6;
    }
    /* value capture is safe: modifying the copy does not touch the array */
    {
        int a[3] = {1, 2, 3};
        for (auto x : a) x += 100;
        if (a[0] != 1 || a[1] != 2 || a[2] != 3) return 7;
    }
    /* member begin()/end() range */
    {
        Vec v;
        v.data[0] = 1; v.data[1] = 2; v.data[2] = 3;
        int s = 0;
        for (auto x : v) s += x;
        if (s != 6) return 8;
    }
    /* struct member array as the range */
    {
        Holder h;
        h.data[0] = 5; h.data[1] = 6; h.data[2] = 7;
        int s = 0;
        for (auto x : h.data) s += x;
        if (s != 18) return 9;
    }
    /* string literal: "abc" is char[4] (NUL included) */
    {
        int n = 0;
        for (auto c : "abc") n++;
        if (n != 4) return 10;
    }
    /* nested range-for */
    {
        int a[3] = {1, 2, 3};
        int b[2] = {10, 20};
        int s = 0;
        for (auto x : a)
            for (auto y : b)
                s += x + y;
        if (s != 102) return 11;   /* (1+10)+(1+20)+...+(3+20) */
    }
    /* break / continue */
    {
        int a[5] = {1, 2, 3, 4, 5};
        int s = 0;
        for (auto x : a) {
            if (x == 3) continue;
            if (x == 5) break;
            s += x;
        }
        if (s != 7) return 12;   /* 1 + 2 + 4 */
    }
    return 0;
}
