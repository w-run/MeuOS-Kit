/* placement_new.cc — placement new (m++).
 *
 * `new (ptr) T(args)` constructs an object at the given address without
 * allocating; the expression yields `(T*)ptr`.  This is the building
 * block for custom allocators / in-place object storage.
 *
 * Covers:
 *  - constructing on a stack buffer and reading through the pointer
 *  - the resulting pointer aliases the supplied buffer (no allocation)
 *  - construction with arguments (non-default constructor)
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
class Counter {
public:
    Counter(int v) { val = v; }
    int val;
};

int
main(void)
{
    char buf[sizeof(Counter)];

    /* construct in the stack buffer with a constructor argument */
    Counter *c = new (buf) Counter(42);
    if (c->val != 42) return 1;

    /* the pointer is exactly the placement buffer: nothing was allocated */
    if ((char *)c != buf) return 2;

    /* a second object can be placed at a different offset */
    char buf2[sizeof(Counter)];
    Counter *d = new (buf2) Counter(7);
    if (d->val != 7) return 3;
    if ((char *)d != buf2) return 4;

    return 0;
}
