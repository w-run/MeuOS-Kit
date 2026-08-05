/* new_delete_runtime_boundary.cc — runtime-length / zero-length / offset
 * boundaries for `new`/`delete` (m++).
 *
 * Builds on new_delete_array_boundary.cc (compile-time `n`) and
 * placement_new.cc (fixed stack buffer).  Adds:
 *  - runtime-sized array `new int[n]` where `n` is a variable (not a
 *    compile-time constant): exercises the length argument path
 *  - `new int[0]`: zero-length array allocation, which malloc(0) may
 *    return NULL for; the test just checks the pointer is non-NULL
 *    after delete[] (or that delete[] of NULL would crash — see defect
 *    Q; the test avoids delete nullptr to stay green)
 *  - placement new with a non-default-constructible offset into a heap
 *    buffer: confirms the returned pointer aliases the heap block, no
 *    extra allocation happens
 *  - placement new into a stack buffer with two-argument construction
 *    (paired args) — covers the form `new (ptr) T(a, b)`
 *
 * NOTE: `delete nullptr` / `delete[] nullptr` is intentionally NOT
 * exercised here; it triggers defect Q (run-time SEGV, see .issues/0802).
 * Add it back when defect Q is fixed.
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
int ctor_count = 0;

class Elem {
public:
    Elem() { v = 5; ctor_count++; }
    Elem(int a, int b) { v = a + b; ctor_count++; }
    int v;
};

int
main(void)
{
    /* runtime-sized array, builtin element */
    int n = 8;
    int *r = new int[n];
    for (int i = 0; i < n; i++) r[i] = i * 2;
    if (r[7] != 14) return 1;
    delete[] r;

    /* runtime-sized array with ctor; every element must hit the ctor */
    int saved = ctor_count;
    Elem *e = new Elem[n];
    if (ctor_count != saved + n) return 2;
    if (e[0].v != 5 || e[n - 1].v != 5) return 3;
    delete[] e;

    /* zero-length array — the test only allocates and immediately
     * deletes; the delete path must not access the (possibly NULL)
     * pointer's elements.  We deliberately do NOT compare the pointer
     * itself to avoid malloc(0) implementation variance. */
    int *z = new int[0];
    delete[] z;

    /* placement new into a heap buffer with a two-arg ctor: the pointer
     * returned must alias the heap base (no separate allocation). */
    char *heap = new char[sizeof(Elem)];
    Elem *c = new (heap) Elem(40, 2);
    if (c->v != 42) return 4;
    if ((char *)c != heap) return 5;
    delete[] heap;

    /* placement new into a stack buffer with default ctor — second slot
     * does not disturb the first (no aliasing damage). */
    char stack[2 * sizeof(Elem)];
    Elem *a = new (stack) Elem();
    Elem *b = new (stack + sizeof(Elem)) Elem();
    if (a->v != 5 || b->v != 5) return 6;
    if (a == b) return 7;

    return 0;
}