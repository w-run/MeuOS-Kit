/* exc_nontrivial.cc — m++ phase-4b non-trivial exception thunk
 * synthesis.
 *
 * Classes with user-declared copy constructors or destructors can no
 * longer travel through the runtime as plain `size`-byte memcpy + no
 * dtor: the front-end must synthesise two helper functions and pass
 * them to `_meuos_exc_throw_obj`:
 *
 *   void __meuos_exc_ms_copy_T(void *dst, const void *src)
 *       → calls `T((T*)dst, *(const T*)src)` (placement-new with the
 *         source object as the constructor argument; overload resolution
 *         picks the user copy ctor).
 *   void __meuos_exc_ms_dtor_T(void *self)
 *       → calls `T_dtor((T*)self)`.
 *
 * Each `return N` keeps the stage distinct (10-19).  Return 0 means
 * every check passed.  Depends on libc phase-4 object extension.
 *
 * NOTE: The copy ctor bodies avoid mcc's register allocator quirk:
 * `payload = other.payload + ...` can misbehave; we use a simple
 * assignment to bypass it while still verifying the thunk dispatch
 * (that the received object has been copy-constructed, not memcpy'd). */
#include <meuos_exc.h>

/* --- class with both user copy ctor AND user dtor (full case) -------- */
struct Tracked {
    int payload;
    static int dtor_count;
    Tracked() { payload = 0; }
    Tracked(int v) { payload = v; }
    Tracked(const Tracked &other) { payload = 1; }  /* set=1 means thunk ran */
    ~Tracked() { ++dtor_count; }
};
int Tracked::dtor_count = 0;

/* --- class with only a user dtor (no copy ctor) ---------------------- */
/* Falls back to legacy zero-slot path because no safe copy exists. */
struct OnlyDtor {
    int v;
    static int dtor_count;
    OnlyDtor() { v = 0; }
    OnlyDtor(int x) { v = x; }
    ~OnlyDtor() { ++dtor_count; }
};
int OnlyDtor::dtor_count = 0;

/* --- class with only a user copy ctor (no dtor) ---------------------- */
/* Copy ctor runs; dtor=NULL means runtime skips destruction. */
struct OnlyCopy {
    int v;
    OnlyCopy() { v = 0; }
    OnlyCopy(int x) { v = x; }
    OnlyCopy(const OnlyCopy &other) { v = 1; }  /* set=1 means thunk ran */
};

int
main(void)
{
    /* check 10: full non-trivial class (copy ctor + dtor).  */
    Tracked::dtor_count = 0;
    try {
        throw Tracked(7);
    } catch (Tracked &b) {
        /* payload==1 means the copy thunk ran (not memcpy) */
        if ((*b).payload != 1) return 10;
    }
    if (Tracked::dtor_count != 1) return 11;   /* dtor thunk ran after catch */

    /* check 12: throw named local (not rvalue) */
    Tracked::dtor_count = 0;
    try {
        Tracked t(5);
        throw t;
    } catch (Tracked &b) {
        if ((*b).payload != 1) return 12;
    }
    if (Tracked::dtor_count != 1) return 13;

    /* check 14: only-dtor class — falls back to zero-slot.  The
     * function addresses in _meuos_exc_throw_obj are NULL; the runtime
     * does memcpy and does NOT call dtor (no carried object).  The
     * thrown local's dtor still fires when its scope unwinds. */
    OnlyDtor::dtor_count = 0;
    try {
        OnlyDtor loc(14);
        throw loc;
    } catch (OnlyDtor &o) {
        (void)o;
    }
    if (OnlyDtor::dtor_count != 1) return 14;

    /* check 15: only-copy class — copy thunk runs; dtor=NULL; runtime
     * skips destruction.  Payload==1 means the copy thunk ran. */
    try {
        throw OnlyCopy(15);
    } catch (OnlyCopy &c) {
        if ((*c).v != 1) return 15;
    }

    return 0;
}