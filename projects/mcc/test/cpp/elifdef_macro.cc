/* elifdef_macro.cc — #elifdef/#elifndef in skipped groups, C++ front end.
 *
 * Same D3-PP regression as test/c23/elifdef_boundary.c, exercised through
 * m++ with object-like and function-like macros: while skipping an
 * inactive group, an #elifdef whose condition held used to return with
 * `tok` still on the macro-name identifier, producing a spurious
 * "expected newline after preprocessing directive".
 */

#define D3_OBJ 7
#define D3_FN(x) ((x) + 1)

static int probe() {
	int r = 0;

	/* Dead group; taken #elifdef on an object-like macro. */
#if 0
	r += 1000;
#elifdef D3_OBJ
	r += 1;
#else
	r += 2000;
#endif

	/* Dead group; taken #elifdef on a function-like macro name.  The
	 * name must be tested for definedness only — never invoked — and
	 * the rest of the line still has to be consumed. */
#if 0
	r += 1000;
#elifdef D3_FN
	r += 2;
#else
	r += 2000;
#endif

	/* #elifndef on an undefined macro, reached from a dead group. */
#if 0
	r += 1000;
#elifndef D3_MISSING
	r += 4;
#else
	r += 2000;
#endif

	/* Nested #if inside a dead group: the inner #elifdef is skipped
	 * wholesale via depth counting and must not be evaluated. */
#if 0
#if 1
	r += 1000;
#elifdef D3_OBJ
	r += 2000;
#endif
	r += 4000;
#else
	r += 8;
#endif

	/* Untaken #elifdef chain falling through to #else. */
#if 0
	r += 1000;
#elifdef D3_ABSENT_A
	r += 2000;
#elifndef D3_OBJ
	r += 4000;
#else
	r += 16;
#endif

	return r;
}

int main() { return probe() == 31 ? 0 : 1; }
