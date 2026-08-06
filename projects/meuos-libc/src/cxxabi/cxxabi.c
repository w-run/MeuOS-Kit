/* cxxabi.c — C++ ABI exception runtime (Itanium C++ ABI).
 *
 * Implements __cxa_allocate_exception / __cxa_throw / __cxa_begin_catch /
 * __cxa_end_catch / __cxa_free_exception and __gxx_personality_v0.
 *
 * These are the Itanium C++ ABI entry points for DWARF-based exception
 * handling.  Currently they delegate to the existing setjmp/longjmp-based
 * MeuOS exception runtime (_meuos_exc_*) which handles dispatch; the
 * DWARF personality returns _URC_CONTINUE_UNWIND so the DWARF unwinder
 * (when present) skips past functions that use setjmp/longjmp-based catch.
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <meuos_exc.h>

/* --- Itanium C++ ABI constants ---------------------------------------- */

#define _URC_CONTINUE_UNWIND    (-2)
#define _URC_HANDLER_FOUND      8
#define _US_ACTION_MASK         3
#define _US_FORCE_UNWIND        1
#define _US_UNWIND_TO_CALLER    2

/* Exception header carried by __cxa_allocate_exception. */
struct __cxa_exception {
	void *obj;                    /* The thrown exception object */
	void *typeinfo;               /* std::type_info* */
	void (*dest)(void *);         /* destructor */
	void *adjustedPtr;            /* adjusted pointer for catch */
};

/* --- Allocation / Free ------------------------------------------------- */

void *
__cxa_allocate_exception(size_t size)
{
	struct __cxa_exception *hdr;
	/* Overallocate: header precedes the thrown object. */
	hdr = malloc(sizeof(struct __cxa_exception) + size);
	if (!hdr)
		abort();
	return hdr + 1;  /* return the object slot after the header */
}

void
__cxa_free_exception(void *obj)
{
	if (!obj)
		return;
	struct __cxa_exception *hdr;
	hdr = ((struct __cxa_exception *)obj) - 1;
	free(hdr);
}

/* --- Throw ------------------------------------------------------------- */

_Noreturn void
__cxa_throw(void *obj, void *typeinfo, void (*dest)(void *))
{
	struct __cxa_exception *hdr;

	hdr = ((struct __cxa_exception *)obj) - 1;
	hdr->obj = obj;
	hdr->typeinfo = typeinfo;
	hdr->dest = dest;

	/* Delegate to the existing MeuOS setjmp/longjmp exception runtime.
	 * The typecode and value are not directly meaningful here (the
	 * setjmp-based dispatch uses _meuos_exc_caught_type/value internally);
	 * propagate the object pointer through the value slot. */
	_meuos_exc_throw_obj(0, sizeof(struct __cxa_exception) +
	                     (hdr->dest ? sizeof(void *) : 0),
	                     sizeof(void *), NULL, dest, 0, obj);
	/* not reached */
	abort();
}

/* --- Catch ------------------------------------------------------------- */

void *
__cxa_begin_catch(void *obj)
{
	/* Return the adjusted pointer to the caught exception.
	 * With setjmp/longjmp dispatch, obj is NULL (the personality
	 * is not invoked), so use the MeuOS runtime accessor. */
	const void *co = _meuos_exc_caught_obj();
	if (!co)
		return NULL;
	/* The caught object is the thrown exception object; return it. */
	return (void *)co;
}

void
__cxa_end_catch(void)
{
	/* Release the runtime-held exception object (dtor + free). */
	_meuos_exc_caught_free();
}

/* --- __cxa_pure_virtual ------------------------------------------------- */

void
__cxa_pure_virtual(void)
{
	abort();
}

/* --- __cxa_guard (thread-safe static local init) ----------------------- */

int
__cxa_guard_acquire(void *g)
{
	/* Minimal: no threading, always succeeds. */
	(void)g;
	return 1;
}

void
__cxa_guard_release(void *g)
{
	(void)g;
}

void
__cxa_guard_abort(void *g)
{
	(void)g;
}

/* --- __cxa_atexit ------------------------------------------------------ */

int
__cxa_atexit(void (*fn)(void *), void *obj, void *dso)
{
	/* Minimal stub: register atexit via plain atexit for now.
	 * Full implementation would manage a per-DSO list. */
	(void)obj;
	(void)dso;
	return atexit((void (*)(void))fn);
}

/* --- Personality (DWARF) ----------------------------------------------- */

/* DWARF personality routine: for the existing setjmp/longjmp exception
 * mechanism, this function tells the DWARF unwinder that no catch block
 * was registered via .gcc_except_table, so the unwinder should continue
 * unwinding. */
int
__gxx_personality_v0(void *state, void *exc, void *ctx)
{
	(void)exc;
	(void)ctx;
	int action = *(int *)state & _US_ACTION_MASK;

	if (action == _US_FORCE_UNWIND || action == _US_UNWIND_TO_CALLER)
		return _URC_CONTINUE_UNWIND;

	/* No landing pad registered via this personality:
	 * continue unwinding. */
	return _URC_CONTINUE_UNWIND;
}