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
 * The runtime then heap-copies the source via the copy thunk, runs the
 * user code paths the user expects, and (after catch consumes the
 * payload) destroys via the dtor thunk.  This file checks the end-to-end
 * behaviour: the runtime receives real function pointers (not NULL), the
 * copy ctor side effect runs once per throw, the dtor side effect runs
 * once after catch, and by-reference / by-value catches both read the
 * copied values back correctly.
 *
 * Each `return N` keeps the stage distinct (10-19).  A return value of
 * 0 means every check passed.  Depends on libc's phase-4 object
 * extension; the make check-cpp-func rule compiles with the
 * meuos-specs/sysroot dispatch that pulls it in.
 */
#include <meuos_exc.h>

/* --- class with both user copy ctor AND user dtor (the full case) -- */
struct Tracked {
    int payload;
    static int ctor_count;   /* tracks copy-ctor side effects */
    static int dtor_count;   /* tracks dtor side effects */
    Tracked() { payload = 0; }
    Tracked(int v) { payload = v; }
    Tracked(const Tracked &other) { payload = other.payload + 1000; ++ctor_count; }
    ~Tracked() { ++dtor_count; }
};
int Tracked::ctor_count = 0;
int Tracked::dtor_count = 0;

/* --- class with only a user dtor (no copy ctor) --------------------- */
/* This combination falls back to the legacy zero-slot path because
 * there is no safe copy (state captured by the dtor would be lost), so
 * the runtime cannot preserve the object across the throw.  We exercise
 * the fall-back to ensure it does not crash. */
struct OnlyDtor {
    int v;
    static int dtor_count;
    OnlyDtor() { v = 0; }
    OnlyDtor(int x) { v = x; }
    ~OnlyDtor() { ++dtor_count; }
};
int OnlyDtor::dtor_count = 0;

/* --- class with only a user copy ctor (no dtor) --------------------- */
/* Copy ctor runs as the runtime payload travels; no dtor needed (the
 * trivial dtor means the runtime skips destruction). */
struct OnlyCopy {
    int v;
    static int ctor_count;
    OnlyCopy() { v = 0; }
    OnlyCopy(int x) { v = x; }
    OnlyCopy(const OnlyCopy &other) { v = other.v + 100; ++ctor_count; }
};
int OnlyCopy::ctor_count = 0;

int
main(void)
{
    /* check 10: full non-trivial class (both copy ctor and dtor).  The
     * runtime must call our copy ctor (so payload gets +1000 and
     * ctor_count increments) and our dtor (dtor_count increments).  A
     * catch by value reads the copied payload; catch(B &b) sees the
     * same value through the slice pointer. */
    Tracked::ctor_count = 0;
    Tracked::dtor_count = 0;
    try {
        throw Tracked(7);
    } catch (Tracked &b) {
        if ((*b).payload != 1007) return 10;     /* copy ctor ran */
        if (Tracked::ctor_count != 1) return 11;  /* exactly one copy */
    }
    if (Tracked::dtor_count != 1) return 12;      /* dtor after catch */

    /* check 13: by-value catch sees the copied state too. */
    Tracked::ctor_count = 0;
    Tracked::dtor_count = 0;
    try {
        throw Tracked(13);
    } catch (Tracked b) {
        if (b.payload != 1013) return 13;
        if (Tracked::ctor_count != 1) return 14;  /* one ctor for the throw */
        if (Tracked::dtor_count != 0) return 15;  /* catch param not yet dtor'd */
    }
    if (Tracked::dtor_count != 2) return 16;      /* throw payload + catch param dtors */

    /* check 17: only-dtor class — fall back path: no copy ctor, so the
     * legacy zero-slot scheme is used (the runtime cannot preserve the
     * object).  The thrown object's dtor must still run when its source
     * scope ends. */
    OnlyDtor::dtor_count = 0;
    try {
        OnlyDtor local(17);
        throw local;
    } catch (OnlyDtor &o) {
        (void)o;
    }
    /* Only the throw-side `local` should have its dtor invoked when its
     * scope ends (the runtime carries no payload).  The catch is by-ref
     * (no copy) and `b` has no dtor side effect to count, so we expect
     * dtor_count == 1 here. */
    if (OnlyDtor::dtor_count != 1) return 17;

    /* check 18: only-copy class — copy ctor runs, no dtor.  ctor_count
     * goes up; dtor is not involved (runtime skips destruction for
     * trivially-destructible classes). */
    OnlyCopy::ctor_count = 0;
    try {
        throw OnlyCopy(18);
    } catch (OnlyCopy &c) {
        if ((*c).v != 118) return 18;             /* copy ctor applied offset */
    }
    if (OnlyCopy::ctor_count != 1) return 19;

    return 0;
}