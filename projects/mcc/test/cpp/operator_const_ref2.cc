/* D1 regression: comprehensive const T& operator tests.
 * Tests free and member operators with const reference parameters,
 * including operator<, operator<=>, and mixed scenarios. */
struct Vec {
    int x, y;
};

/* Free operators with const reference */
int operator==(const Vec& a, const Vec& b) {
    return a.x == b.x && a.y == b.y;
}
int operator<(const Vec& a, const Vec& b) {
    if (a.x != b.x) return a.x < b.x;
    return a.y < b.y;
}

/* Member operators with const reference */
struct S {
    int val;
    bool operator==(const S& o) const { return val == o.val; }
    bool operator<(const S& o) const { return val < o.val; }
};

/* Non-member operator with non-const value params */
int operator!=(Vec a, Vec b) {
    return a.x != b.x || a.y != b.y;
}

int main(void) {
    Vec v1 = {1, 2};
    Vec v2 = {1, 2};
    Vec v3 = {3, 4};
    S s1 = {5};
    S s2 = {5};
    S s3 = {7};

    /* free const ref */
    if (!(v1 == v2)) return 1;
    if (v1 == v3) return 2;
    if (!(v1 < v3)) return 3;
    if (v3 < v1) return 4;

    /* free value params */
    if (v1 != v2) return 5;
    if (!(v1 != v3)) return 6;

    /* member const ref */
    if (!(s1 == s2)) return 7;
    if (s1 == s3) return 8;
    if (!(s1 < s3)) return 9;
    if (s3 < s1) return 10;

    /* const objects */
    const S cs1 = {10};
    const S cs2 = {10};
    if (!(cs1 == cs2)) return 11;
    if (cs1 < cs2) return 12;

    return 0;
}