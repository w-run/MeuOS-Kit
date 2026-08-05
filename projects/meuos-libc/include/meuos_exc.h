#ifndef MEUOS_EXC_H
#define MEUOS_EXC_H

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

/* Read the exception that activated the current catch. */
int _meuos_exc_caught_type(void);
unsigned long long _meuos_exc_caught_value(void);

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_EXC_H */
