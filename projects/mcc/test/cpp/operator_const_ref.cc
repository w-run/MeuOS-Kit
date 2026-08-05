/* D1 regression: const T& operator parameter resolution.
 * operator==(const Vec&, const Vec&) must compile and run. */
struct Vec {
    int x, y;
};

/* Free operator== with const reference parameters */
int operator==(const Vec& a, const Vec& b) {
    return a.x == b.x && a.y == b.y;
}

/* Member operator== with const reference parameter */
struct S {
    int val;
    bool operator==(const S& o) const {
        return val == o.val;
    }
};

int main(void) {
    Vec v1 = {1, 2};
    Vec v2 = {1, 2};
    Vec v3 = {3, 4};

    /* free operator== with const ref */
    if (!(v1 == v2)) return 1;
    if (v1 == v3) return 2;

    /* member operator== with const ref */
    S s1 = {5};
    S s2 = {5};
    S s3 = {7};
    if (!(s1 == s2)) return 3;
    if (s1 == s3) return 4;

    /* const object member call */
    const S cs1 = {10};
    const S cs2 = {10};
    if (!(cs1 == cs2)) return 5;

    return 0;
}