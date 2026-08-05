/* const_cast: casting away constness is legal C++ (the *cast* itself is not a
 * diagnostic — modifying an originally-const object through the result is
 * undefined behaviour at runtime, exactly as GCC/Clang accept).  This positive
 * test verifies const_cast drops a const qualifier so the converted pointer
 * reads the underlying value through the (non-const) lvalue.
 */
#include <stdio.h>

int main(void) {
    int x = 42;
    const int *cp = &x;
    int *p = const_cast<int *>(cp);   /* const_cast removes const from pointee */
    *p = 99;                          /* x is not originally-const: OK */
    return (x == 99) ? 0 : 1;
}
