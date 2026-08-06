/* exc_base_slice.cc — m++ phase-4b base-subobject slicing for
 * single-inheritance throw/catch.
 *
 * Stage-4 already covers multi-inheritance (the 2nd base at a non-zero
 * offset) inside try_catch.cc via exc_slice_ptr; this file targets the
 * trivial single-inheritance base offset (`exc_base_offset`):
 *
 *   struct B { int x; };
 *   struct D : B { int y; };
 *
 * `throw D()` carries a Derived object through the runtime; `catch(B&)`
 * matches the base type and the mcc-side slice must re-aim the carried
 * pointer at the Base sub-object so the catch body reads `b.x` from
 * offset 0 (Base sub-object == head of the Derived object in the single
 * non-empty base case).  Stage 4b also exercises a chained inheritance
 * (`struct Mid : B; struct D2 : Mid`) to ensure the slice walks through
 * the anonymous base chain, plus exact-type catch (the runtime carries
 * the full object, no slice offset).
 *
 * Like try_catch.cc, this depends on libc's phase-4 object extension
 * (`_meuos_exc_throw_obj` + `meuos_exc.h`); check-cpp-func compiles it
 * with the same meuos-specs / sysroot / libc-include dispatch as
 * try_catch.cc.  Each `return N` keeps the stage distinct (24-29).
 *
 * A return value of 0 means every check below passed.
 */
#include <meuos_exc.h>

/* --- single inheritance: Base sits at offset 0 (the head) ---------- */
struct B { int x; B() : x(0xB0) {} };
struct D : B { int y; D() : y(0xD0) {} };

/* --- chained inheritance: B sits inside Mid inside D2 ------------ */
struct Mid : B { int m; Mid() : m(0x10) {} };
struct D2 : Mid { int z; D2() : z(0x20) {} };

/* --- exact-type derived catch (no slicing needed) ------------------ */
struct Plain { int p; Plain() : p(0xCC) {} };

/* --- second-base at offset sizeof(First) --------------------------- */
struct First { int fa; First() : fa(11) {} };
struct Second { int sb; Second() : sb(22) {} };
struct Both : First, Second { int dd; Both() : dd(33) {} };

int
main(void)
{
    /* check 24: single-inheritance catch(Base&) reads base member
     * through the slice.  throw(D()) stores the full D object; the
     * mcc-side slice re-aims the carried pointer at offset 0 (where
     * the Base sub-object of D starts). */
    {
        try {
            throw D();
        } catch (B &b) {
            if ((*b).x != 0xB0) return 24; /* uninitialized Base.x */
        }
    }

    /* check 25: catch(Base&) of a chained D2 still sees the Base
     * sub-object — the slice walks Mid → B (both anonymous members
     * at offset 0 within their enclosing type). */
    {
        try {
            throw D2();
        } catch (B &b) {
            if ((*b).x != 0xB0) return 25;
        }
    }

    /* check 26: exact-type derived catch by reference (no slice —
     * ctype == thrown type, the carried pointer IS the D view). */
    {
        try {
            throw D();
        } catch (D &d) {
            if ((*d).y != 0xD0) return 26;
        }
    }

    /* check 27: multi-inheritance catch(Second&) of a throw(Both) —
     * Second sits at offset sizeof(First); the slice re-aims to
     * (char*)carried + sizeof(First). */
    {
        try {
            throw Both();
        } catch (Second &s) {
            if ((*s).sb != 22) return 27;
        }
    }

    /* check 28: multi-inheritance catch(First&) — First is at
     * offset 0, but the derived Both is registered with its own
     * typecode, so the slice still picks First's offset (0). */
    {
        try {
            throw Both();
        } catch (First &f) {
            if ((*f).fa != 11) return 28;
        }
    }

    /* check 29: catch-by-value of a base slices the same way as the
     * by-ref path — copies from the base sub-object bytes, not the
     * head of the derived object (defensive: a 2nd-base by-value copy
     * would otherwise read First's bytes for a Second handler). */
    {
        try {
            throw Both();
        } catch (Second s) {
            if (s.sb != 22) return 29;
        }
    }

    return 0;
}