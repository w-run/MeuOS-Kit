/* new_delete_nullptr.cc — `delete nullptr` / `delete[] nullptr` must be a
 * no-op (C++ [expr.delete]): neither a destructor nor the allocator is
 * touched for a null operand (m++ end-to-end).
 *
 * Regression canary for defect Q (run-time SEGV on `delete nullptr`):
 *  - scalar `delete` on a null pointer must not run the class destructor
 *  - `delete[]` on a null pointer must not read the array-length cookie
 *    at `(char*)0 - sizeof(size_t)` nor free a bogus offset
 *  - the same guards must not disturb non-null deletes (heap reuse)
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
int dtor_runs = 0;
int ctor_runs = 0;

class Elem {
public:
    Elem() { v = 9; ctor_runs++; }
    ~Elem() { dtor_runs++; }
    int v;
};

static Elem *make_elem(void)
{
    Elem *e = new Elem;
    if (e->v != 9) return 0;
    return e;
}

int
main(void)
{
    /* scalar delete of a null pointer: no dtor, no crash */
    int *pi = 0;
    delete pi;
    if (dtor_runs != 0) return 1;

    Elem *pe = 0;
    delete pe;
    if (dtor_runs != 0) return 2;

    /* array delete[] of a null pointer: no cookie read, no crash */
    int *ai = 0;
    delete[] ai;
    if (dtor_runs != 0) return 3;

    Elem *ae = 0;
    delete[] ae;
    if (dtor_runs != 0) return 4;

    /* non-null deletes still behave: destructor runs exactly once per
     * element, and the freed storage is reusable */
    Elem *x = make_elem();
    if (!x) return 5;
    delete x;
    if (dtor_runs != 1) return 6;

    Elem *arr = new Elem[3];
    if (ctor_runs != 4) return 7;   /* 1 make_elem + 3 array */
    delete[] arr;
    if (dtor_runs != 4) return 8;   /* 1 + 3 */

    /* heap reuse after delete: the new object must be live */
    Elem *y = new Elem;
    if (y->v != 9) return 9;
    delete y;
    if (dtor_runs != 5) return 10;

    return 0;
}
