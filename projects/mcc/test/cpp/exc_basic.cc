/* exc_basic.cc — C++ exceptions: basic frontend support (throw lowering).
 *
 * This milestone wires the `throw` keyword into the C++ frontend: it
 * lowers `throw expr;` (and bare `throw;`) to a call of the exception
 * runtime entry `_meuos_exc_throw(int typecode, unsigned long long value)`.
 * The full landingpad / .eh_frame unwinder that would route a thrown
 * exception to a `catch` block is a separate backend follow-up (see the
 * `try` diagnostic).
 *
 * Here the runtime entry is stubbed inline to observe that the thrown
 * value crosses the ABI correctly.  Returns 0 on success.
 */

static long g_thrown = 0;

extern "C" void
_meuos_exc_throw(int typecode, unsigned long long value)
{
	(void)typecode;
	g_thrown = (long)value;
}

void
throw_it(int x)
{
	throw x;
}

int
main(void)
{
	g_thrown = 0;
	throw_it(42);
	if (g_thrown != 42) return 1;   /* the thrown value reached the runtime */

	g_thrown = 0;
	throw_it(-7);
	if (g_thrown != -7) return 2;

	return 0;
}
