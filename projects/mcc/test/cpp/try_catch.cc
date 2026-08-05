/* try_catch.cc — m++ phase-1 exception: in-function `try {} catch(int)`
 * lowered onto the libc setjmp/longjmp runtime (meuos_exc.h): a throw in
 * the try body is caught, the catch parameter reads the thrown value, and
 * a try whose body never throws runs its normal path and pops the handler.
 *
 * Requires <meuos_exc.h> on the include path (libc-provided).  Compiled as
 * a freestanding user program with m++ --specs=host.
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 *
 * NOTE: this file is currently excluded from check-cpp-func's wildcard
 * (Makefile greps out /try_catch\.cc) because it depends on <meuos_exc.h>
 * and the setjmp/longjmp exception runtime, which land on the libc side
 * (libc-worker's tmp/libc-work, not yet merged to main).  Once the libc
 * exception foundation is merged, drop the filter and add
 * `-I../meuos-libc/include` so this joins the gate.
 */
#include <meuos_exc.h>

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

    return 0;
}
