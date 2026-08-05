#ifndef MEUOS_EXC_H
#define MEUOS_EXC_H

#include <features.h>
#include <setjmp.h>
#include <stdint.h>
#include <stddef.h>

/* MeuOS exception runtime — first-phase foundation (setjmp/longjmp based).
 *
 * m++ lowers `throw` / `catch` onto these helpers; this is the system-
 * runtime counterpart to glibc's __cxa_throw but MeuOS-specific.  The ABI is
 * aligned with mcc-worker's planned lowering:
 *
 *   throw expr -> _meuos_exc_throw(typecode, (unsigned long long)value)
 *   try frame  -> caller declares a local _meuos_exc_frame, setjmps into its
 *                 embedded jmp_buf, then registers it; unwinds via longjmp.
 *   catch read -> _meuos_exc_caught_type() / _meuos_exc_caught_value()
 *
 * A throwing call must have a registered handler or the program aborts
 * (uncaught exception), matching C++ `terminate`.
 */

typedef struct _meuos_exc_frame {
	jmp_buf env;                    /* setjmp/longjmp env for this try frame */
	struct _meuos_exc_frame *prev;  /* enclosing handler, or NULL at top */
} _meuos_exc_frame;

#ifdef __cplusplus
extern "C" {
#endif

/* Push `frame` as the current exception handler.  Call this AFTER setjmp
 * into frame->env so a longjmp from a nested throw returns here. */
void _meuos_exc_try_begin(_meuos_exc_frame *frame);

/* Pop the current handler (normal completion / leaving the try scope). */
void _meuos_exc_try_end(void);

/* Raise an exception: persist typecode+value, longjmp to the innermost
 * registered handler; abort() if none (uncaught).  Never returns. */
_Noreturn void _meuos_exc_throw(int typecode, unsigned long long value);

/* Object-payload throw (phase 4): carry an arbitrarily-sized object (not
 * just a u64 scalar) from throw to catch.  The runtime heap-allocates an
 * aligned buffer, copies the object into it (calling `copy` if non-NULL,
 * else memcpy), destroys the source temporary (via `dtor` if non-NULL), then
 * unwinds exactly like the scalar throw.  `align` selects the object's
 * alignment; `offset` is the base-subobject offset (base/catch slicing;
 * 0 for the first increment).  Trivial classes pass copy=NULL (memcpy) and
 * dtor=NULL (no destruction).  Never returns. */
_Noreturn void _meuos_exc_throw_obj(int typecode, size_t size, size_t align,
                                    void (*copy)(void *, const void *),
                                    void (*dtor)(void *),
                                    int offset, const void *obj);

/* Read the exception that activated the current catch. */
int _meuos_exc_caught_type(void);
unsigned long long _meuos_exc_caught_value(void);

/* Object-payload catch accessors (phase 4): the current exception object
 * pointer (to build/rebind the catch parameter) and release of the
 * runtime-held heap object after the catch consumes it.  _caught_is_obj
 * reports whether the active exception is an object (vs scalar).
 * _meuos_exc_caught_free is idempotent and safe for a scalar exception. */
const void *_meuos_exc_caught_obj(void);
void _meuos_exc_caught_free(void);
int _meuos_exc_caught_is_obj(void);

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_EXC_H */
