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
#include <string.h>
#include <stdint.h>
#include <meuos_exc.h>

static _Thread_local _meuos_exc_frame *exc_head;
static _Thread_local int exc_typecode;
static _Thread_local unsigned long long exc_value;
/* Object-payload persist (phase 4): raw malloc base + aligned object ptr +
 * dtor.  Scalar throws leave exc_objbuf == NULL. */
static _Thread_local void *exc_objbuf;     /* raw malloc'd buffer (for free) */
static _Thread_local const void *exc_obj;  /* aligned object pointer */
static _Thread_local void (*exc_objdtor)(void *);

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

/* ---- phase-4 object payload ---- */

_Noreturn void
_meuos_exc_throw_obj(int typecode, size_t size, size_t align,
                     void (*copy)(void *, const void *),
                     void (*dtor)(void *), int offset, const void *obj)
{
	_meuos_exc_frame *handler;
	void *buf;
	uintptr_t base, aligned;
	void *slot;

	(void)offset; /* base-subobject slicing is a later increment (0 for now) */

	/* Heap-allocate an aligned buffer to carry the object through the
	 * longjmp (stack objects cannot survive unwind).  malloc already returns
	 * max_align_t-aligned memory; for over-aligned types, align manually by
	 * overallocating and adjusting. */
	if (align < sizeof(void *))
		align = sizeof(void *);
	buf = malloc(size + align + sizeof(uintptr_t));
	if (!buf)
		abort();
	base = (uintptr_t)buf;
	aligned = (base + align - 1) & ~(uintptr_t)(align - 1);
	slot = (void *)aligned;

	if (copy)
		copy(slot, obj);               /* copy-construct into heap */
	else
		memcpy(slot, obj, size);       /* trivial bitwise copy */

	/* Destroy the source temporary (the throw-expression object).  The
	 * throw_obj call is _Noreturn, so this is the only destruction. */
	if (dtor)
		dtor((void *)obj);

	/* Persist payload before unwinding. */
	exc_typecode = typecode;
	exc_value = (uintptr_t)slot;
	exc_objbuf = buf;
	exc_obj = slot;
	exc_objdtor = dtor;

	handler = exc_head;
	if (!handler)
		abort();                       /* uncaught exception */

	exc_head = handler->prev;
	longjmp(handler->env, 1);
	abort();
}

const void *
_meuos_exc_caught_obj(void)
{
	return exc_obj;
}

int
_meuos_exc_caught_is_obj(void)
{
	return exc_objbuf != NULL;
}

void
_meuos_exc_caught_free(void)
{
	if (!exc_objbuf)
		return;                        /* scalar exception: nothing to free */
	if (exc_objdtor)
		exc_objdtor((void *)exc_obj);  /* destroy the carried object */
	free(exc_objbuf);
	exc_objbuf = NULL;
	exc_obj = NULL;
	exc_objdtor = NULL;
}
