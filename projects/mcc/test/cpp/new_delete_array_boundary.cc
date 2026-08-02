/* new_delete_array_boundary.cc — array new/delete boundary cases (m++).
 *
 * Covers:
 *  - `new int[10]` on a builtin element type (malloc-only path, no ctor)
 *  - a class with a ctor but NO destructor: `new T[n]` still value-constructs
 *    every element (each ctor call must hit the right element slot), and
 *    `delete[]` just frees the block
 *  - a class with a dtor that reads `this`: `delete[]` must destruct each
 *    element at its correct address (per-element dtor sum checks this)
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
int dtor_sum = 0;

class NoDtor {
public:
    NoDtor() { val = 7; }
    int val;
};

class WithDtor {
public:
    WithDtor() { val = 10; }
    ~WithDtor() { dtor_sum += val; }
    int val;
};

int
main(void)
{
    /* builtin element type: full array is usable after allocation */
    int *a = new int[10];
    for (int i = 0; i < 10; i++)
        a[i] = i * 3;
    if (a[0] != 0 || a[9] != 27) return 1;
    delete[] a;

    /* class with ctor but no dtor: every element must be constructed at
     * its own slot (a 4-element array catches the pointer-scaling bug) */
    NoDtor *nd = new NoDtor[4];
    if (nd[0].val != 7 || nd[1].val != 7 || nd[2].val != 7 || nd[3].val != 7)
        return 2;
    delete[] nd;

    /* class whose dtor reads this: delete[] must visit each element at
     * its correct address, so dtor_sum accumulates the true values */
    WithDtor *wd = new WithDtor[3];
    if (wd[0].val != 10 || wd[2].val != 10) return 3;
    wd[1].val = 20;
    delete[] wd;
    if (dtor_sum != 10 + 20 + 10) return 4;

    return 0;
}
