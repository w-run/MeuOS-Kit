/* exc_base_slice.cc — m++ phase-4b base-subobject slicing for
 * single-inheritance throw/catch.
 *
 * Stage-4 already covers multi-inheritance via try_catch.cc; this file
 * targets the trivial single-inheritance base offset (exc_base_offset):
 *   struct B { int x; };
 *   struct D : B { int y; };
 * `throw D()` carries a Derived object; `catch(B&)` matches the base type
 * and the mcc-side slice re-aims at the Base sub-object.
 *
 * Like try_catch.cc, this depends on libc's phase-4 object extension;
 * check-cpp-func compiles it with the same meuos-specs/sysroot dispatch.
 * Each `return N` keeps the stage distinct (24-30).
 * A return value of 0 means every check passed.
 */
#include <meuos_exc.h>

/* --- single inheritance: Base sits at offset 0 (the head) ---------- */
struct B { int x; };
struct D : B { int y; };

/* --- chained inheritance: B sits inside Mid inside D2 ------------ */
struct Mid : B { int m; };
struct D2 : Mid { int z; };

/* --- exact-type derived catch (no slicing needed) ------------------ */
struct Plain { int p; };

/* --- second-base at offset sizeof(First) --------------------------- */
struct First { int fa; };
struct Second { int sb; };
struct Both : First, Second { int dd; };

int
main(void)
{
    /* check 24: single-inheritance catch(Base&) reads base member
     * through the slice.  B sits at offset 0 inside D. */
    {
        try {
            throw D();
        } catch (B &b) {
            (*b).x = 0x42;
            if ((*b).x != 0x42) return 24;
        }
    }

    /* check 25: catch(Base&) of a chained D2 slices through Mid -> B. */
    {
        try {
            throw D2();
        } catch (B &b) {
            (*b).x = 0x55;
            if ((*b).x != 0x55) return 25;
        }
    }

    /* check 26: exact-type derived catch by reference (no slice). */
    {
        try {
            throw D();
        } catch (D &d) {
            (*d).y = 0xDD;
            if ((*d).y != 0xDD) return 26;
        }
    }

    /* check 27: multi-inheritance catch(Second&) of a throw(Both) —
     * Second sits at offset sizeof(First). */
    {
        try {
            throw Both();
        } catch (Second &s) {
            (*s).sb = 27;
            if ((*s).sb != 27) return 27;
        }
    }

    /* check 28: multi-inheritance catch(First&) — First at offset 0. */
    {
        try {
            throw Both();
        } catch (First &f) {
            (*f).fa = 28;
            if ((*f).fa != 28) return 28;
        }
    }

    return 0;
}