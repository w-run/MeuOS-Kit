/* loongarch64 FP-constant .rodata gate.
 *
 * LoongArch has no 64-bit immediate for an IEEE-754 double (or a 32-bit
 * float) bit pattern, so the backend must stash every FP constant into
 * .rodata and load it with fld.s/fld.d via its address.  The pre-fix
 * emitter materialised the constant with `li.d $t0, 0x0; movgr2fr.w`
 * from garbage bytes (the bit pattern is larger than a 32-bit li.d
 * immediate), so `double d = 1.5; return (int)(d*28)` returned 0 instead
 * of 42 (runtime-matrix rr_fp).  This gate asserts the .rodata stash and
 * the fld load for both a double constant and a float-typed one.
 */
int main(void) {
    double d = 1.5;
    float  f = 2.25f;
    if ((int)(d * 28) != 42)
        return 0;
    if ((int)(f * 2) != 4)
        return 0;
    return 42;
}
