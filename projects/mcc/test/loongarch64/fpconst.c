/* loongarch64 FP constant materialization.
 *
 * Double and float constants cannot be built as integer immediates (a
 * double bit pattern is 64 bits, li.d only encodes 32), so they must be
 * stashed in .rodata and loaded with fld.d/fld.s.  Reading only the low
 * half of the constant used to materialize 1.5 and 28.0 as integer 0. */

double dmul(void) { double d = 1.5; return d * 28.0; }

float fmul(void) { float f = 2.5f; return f * 4.0f; }
