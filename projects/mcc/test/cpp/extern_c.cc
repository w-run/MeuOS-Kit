/* C++ extern "C" linkage specification.
 * Tests both block form and single-declaration form. */
extern "C" {
    int c_add(int a, int b) {
        return a + b;
    }
    int c_mul(int a, int b) {
        return a * b;
    }
}

extern "C" int c_sub(int a, int b);

int c_sub(int a, int b) {
    return a - b;
}

/* C++ function (not extern "C") for comparison */
int cpp_add(int a, int b) {
    return a + b;
}

int main(void) {
    if (c_add(3, 4) != 7) return 1;
    if (c_mul(3, 4) != 12) return 2;
    if (c_sub(10, 3) != 7) return 3;
    return 0;
}