/* Negative test: array allocation/deallocation must be rejected — m++
 * reports "new T[n] (array allocation) is not supported yet" and
 * "'delete[]' (array deallocation) is not supported yet".  This file
 * exercises both forms.  check-cpp-neg compiles it expecting failure.
 */
class C {
public:
    C() { v = 0; }
    int v;
};

int main(void) {
    C *arr = new C[3];   /* unsupported: array allocation */
    arr[0].v = 1;
    delete[] arr;        /* unsupported: array deallocation */
    return arr[0].v == 1 ? 0 : 1;
}
