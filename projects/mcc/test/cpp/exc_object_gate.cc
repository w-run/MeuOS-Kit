// Phase-4 object-payload gate (exc-phase4-object-payload.md): with the
// object runtime declared, a class-typed throw must lower to
// `_meuos_exc_throw_obj(tc, size, align, copy, dtor, offset, &obj)` so the
// object (not just the type code) travels.  The check-cpp-exc-object rule
// compiles this to asm and asserts the throw_obj call appears; the runtime
// linkage is provided by libc's phase-4 extension.
#include <setjmp.h>
typedef struct _meuos_exc_frame { jmp_buf env; struct _meuos_exc_frame *prev; } _meuos_exc_frame;
extern "C" void _meuos_exc_try_begin(_meuos_exc_frame*);
extern "C" void _meuos_exc_try_end(void);
extern "C" _Noreturn void _meuos_exc_throw(int, unsigned long long);
extern "C" _Noreturn void _meuos_exc_throw_obj(int, unsigned long, unsigned long,
    void (*)(void*, const void*), void (*)(void*), int, const void*);
extern "C" int _meuos_exc_caught_type(void);
extern "C" unsigned long long _meuos_exc_caught_value(void);
extern "C" const void *_meuos_exc_caught_obj(void);
extern "C" void _meuos_exc_caught_free(void);
extern "C" int _meuos_exc_caught_is_obj(void);

struct MyObj { int x; };          /* trivial class (no dtor) */
int f(void) {
  /* must emit _meuos_exc_throw_obj(...) AND (on the catch side)
   * _meuos_exc_caught_is_obj / _meuos_exc_caught_obj / _meuos_exc_caught_free */
  try { throw MyObj(); }
  catch (MyObj e) { return e.x; }
  return 0;
}
/* catch-by-reference binds to the runtime object (no copy).  Body accesses
 * through (*e).x because mcc has no real reference type; it lowers T& as T*. */
int g(void) {
  try { throw MyObj(); }
  catch (MyObj &e) { return (*e).x; }
  return 0;
}
/* Same-type scalar catch (the canonical C++ case): a mismatch like
 * throw(int) catch(short) does NOT match per C++ (no implicit scalar
 * promotion in catch), so it is intentionally not covered here. */
int h(void) {
  try { throw (int) 42; }
  catch (int e) { return e; }
  return 0;
}
