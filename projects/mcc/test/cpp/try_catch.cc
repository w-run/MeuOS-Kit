/* try_catch.cc — m++ exception stage 1-2: `try {} catch(...)` lowered onto
 * the libc setjmp/longjmp runtime (meuos_exc.h).
 *
 * Stage 1: a throw in the try body is caught, the catch parameter reads the
 * thrown value, and a try whose body never throws runs its normal path and
 * pops the handler.
 *
 * Stage 2: the throw may cross a function boundary (setjmp-chain longjmp
 * returns to the innermost try), and catch sequences dispatch by type code
 * (cpp_exc_typecode assigns a distinct code per thrown type) so the right
 * `catch (T e)` runs.
 *
 * Requires <meuos_exc.h> on the include path (libc-provided).  Compiled as
 * a freestanding user program with m++ --specs=host.
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 *
 * check-cpp-func compiles it with `-I../meuos-libc/include` so <meuos_exc.h>
 * (the libc exception runtime header) resolves when it is present in the
 * libc project; if the header is absent the target is skipped rather than
 * failing the gate.
 */
#include <meuos_exc.h>

/* stage-3: a small class hierarchy for base/derived catch matching. */
struct ExcB { int b; ExcB() : b(0) {} };
struct ExcD : ExcB { int d; ExcD() : d(1) {} };

/* stage-2: a throw from a callee crosses the function boundary. */
static void
may_throw_cross(int v)
{
    throw v;
}

int
main(void)
{
    /* throw caught by catch(int): the value crosses the runtime */
    try {
        throw 42;
    } catch (int e) {
        if (e != 42) return 1;
    }

    /* a different thrown value arrives intact */
    try {
        throw 123;
    } catch (int e) {
        if (e != 123) return 2;
    }

    /* a try whose body does NOT throw runs the normal path (try_end pops) */
    {
        int x = 0;
        try {
            x = 7;
        } catch (int e) {
            return 3; /* unreachable: no throw */
        }
        if (x != 7) return 4;
    }

    /* stage-2 cross-function: throw in a callee is caught by the try here */
    try {
        may_throw_cross(42);
    } catch (int e) {
        if (e != 42) return 5;
    }

    /* stage-2 multi-catch: throw(int) dispatches to catch(int), not catch(long) */
    try {
        throw 9;
    } catch (long e) {
        return 6; /* should not run: thrown type is int */
    } catch (int e) {
        if (e != 9) return 7;
    }

    /* stage-2 multi-catch: throw(long) dispatches to catch(long) */
    try {
        throw 7L;
    } catch (int e) {
        return 8; /* should not run: thrown type is long */
    } catch (long e) {
        if (e != 7L) return 9;
    }

    /* stage-3: catch(Base) matches a throw(Derived) — single-inheritance
     * base catch by type code (no full RTTI/typeinfo) */
    {
        int inhit = 0;
        try {
            throw ExcD();
        } catch (ExcB e) {
            inhit = 1; /* base catch of a derived throw */
        }
        if (!inhit) return 10;
    }

    /* stage-3: catch(...) swallows any thrown value */
    {
        int all = 0;
        try {
            throw 42;
        } catch (long e) {
            return 11; /* should not run: type is int */
        } catch (...) {
            all = 1;
        }
        if (!all) return 12;
    }

    /* stage-3: explicit bare `throw;` rethrows to an OUTER handler (the
     * inner try must not re-catch it — verified by it terminating) */
    {
        int rh;
        /* outer handler */
        try {
            void inner(void);
            /* inline inner try that rethrows */
            try {
                throw 42;
            } catch (int e) {
                throw; /* rethrow current exception */
            }
            return 13; /* unreachable: rethrow skipped past this block */
        } catch (int e) {
            rh = (e == 42) ? 1 : 0;
        }
        if (!rh) return 14;
    }

    return 0;
}
