/* meuos_exc.c — MeuOS exception runtime, first-phase foundation.
 *
 * A thread-local handler stack of jmp_bufs plus a single-value slot carry a
 * raised exception from _meuos_exc_throw to the innermost catch:
 *   try_begin registers the frame, throw stores typecode/value and longjmps
 *   into the most recent handler, try_end pops on normal exit, and the
 *   caught accessors read the slot.  Uncaught throws call abort() (C++
 *   terminate semantics).  Uses the existing per-arch setjmp/longjmp.
 */

#include <stdlib.h>
#include <meuos_exc.h>

static _Thread_local _meuos_exc_frame *exc_head;
static _Thread_local int exc_typecode;
static _Thread_local unsigned long long exc_value;

void
_meuos_exc_try_begin(_meuos_exc_frame *frame)
{
	frame->prev = exc_head;
	exc_head = frame;
}

void
_meuos_exc_try_end(void)
{
	if (exc_head)
		exc_head = exc_head->prev;
}

_Noreturn void
_meuos_exc_throw(int typecode, unsigned long long value)
{
	_meuos_exc_frame *handler;

	/* Persist the payload before unwinding. */
	exc_typecode = typecode;
	exc_value = value;

	handler = exc_head;
	if (!handler)
		abort();                 /* uncaught exception */

	/* Pop the handler we are jumping into so a subsequent throw in the
	 * catch body targets an outer frame. */
	exc_head = handler->prev;
	longjmp(handler->env, 1);
	/* not reached */
	abort();
}

int
_meuos_exc_caught_type(void)
{
	return exc_typecode;
}

unsigned long long
_meuos_exc_caught_value(void)
{
	return exc_value;
}
