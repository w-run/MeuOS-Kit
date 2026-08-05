/* libgcc_int.h — internal shared types/helpers for libgcc-meuos.
 *
 * These implement the libgcc ABI soft helpers that gcc/clang emit calls to
 * when targeting a sysroot without a vendor libgcc: 64/128-bit division,
 * modulo, multiply, bit-ops (clz/ctz/popcount/parity/bswap) and int<->float
 * conversions.  Everything is implemented in dependency-free C so the
 * archive has no libc or libgcc runtime dependencies.
 *
 * The div/mod routines use restore-by-iteration (shift-subtract) rather than
 * the C `/` and `%` operators on the operand type, so compiling this file
 * never re-emits the very helper being defined (no recursion).
 */

#ifndef LIBGCC_MEUOS_INT_H
#define LIBGCC_MEUOS_INT_H

typedef unsigned int uword;
typedef unsigned long long du_int;   /* 64-bit unsigned */
typedef long long dword;             /* 64-bit signed */

/* 128-bit types (gcc's TImode).  Represented as two 64-bit halves; the
 * __multi3/__udivti3/... helpers operate on these via 64-bit primitives
 * that are themselves provided here (no __int128 compiler magic, so it is
 * toolchain-agnostic). */
typedef struct {
	du_int lo;   /* low 64 bits */
	du_int hi;   /* high 64 bits */
} uti_int;

static inline dword
__negdi2_op(dword a)
{
	return (dword)0 - a;
}

#endif /* LIBGCC_MEUOS_INT_H */
