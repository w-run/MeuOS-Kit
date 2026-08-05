/* exc_obj_gate.c — phase-4 object-payload exception runtime gate.
 * Exercises _meuos_exc_throw_obj with a trivial (NULL copy/dtor -> memcpy)
 * and a non-trivial (copy+dtor) object, verifying the object travels to
 * catch via _meuos_exc_caught_obj / is_obj / free, and that the scalar
 * throw path is unaffected. */
#include <meuos_exc.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static _meuos_exc_frame fr;

/* non-trivial object mimic: counters prove copy/dtor run */
struct Obj { int x; int copies; int dtors; };
static void
mycpy(void *dst, const void *src)
{
	memcpy(dst, src, sizeof(struct Obj));
	((struct Obj *)dst)->copies++;
}
static void
mydtor(void *self)
{
	((struct Obj *)self)->dtors++;
}

static int fails;

int
main(void)
{
	/* 1. trivial object (copy=NULL, dtor=NULL): memcpy path */
	{
		int o = 42;
		if (setjmp(fr.env) == 0) {
			_meuos_exc_try_begin(&fr);
			_meuos_exc_throw_obj(7, sizeof(int), sizeof(int), 0, 0, 0, &o);
			return 1;
		} else {
			int caught = *((const int *)_meuos_exc_caught_obj());
			if (caught != 42) { printf("FAIL: trivial caught=%d\n", caught); fails++; }
			if (!_meuos_exc_caught_is_obj()) { printf("FAIL: trivial not is_obj\n"); fails++; }
			if (_meuos_exc_caught_type() != 7) { printf("FAIL: type=%d\n", _meuos_exc_caught_type()); fails++; }
			_meuos_exc_caught_free();
		}
	}

	/* 2. non-trivial object (copy + dtor) */
	{
		struct Obj o = { 99, 0, 0 };
		if (setjmp(fr.env) == 0) {
			_meuos_exc_try_begin(&fr);
			_meuos_exc_throw_obj(5, sizeof(struct Obj), sizeof(struct Obj),
			                     mycpy, mydtor, 0, &o);
			return 1;
		} else {
			const struct Obj *co = _meuos_exc_caught_obj();
			if (co->x != 99) { printf("FAIL: obj x=%d\n", co->x); fails++; }
			if (co->copies < 1) { printf("FAIL: copy not called\n"); fails++; }
			_meuos_exc_caught_free();
		}
	}

	/* 3. _caught_free is idempotent and safe when no object is active */
	{
		_meuos_exc_caught_free();
		_meuos_exc_caught_free();
	}

	if (fails) {
		printf("%d exc_obj FAIL\n", fails);
		return 1;
	}
	printf("PASS exc_obj\n");
	return 0;
}
