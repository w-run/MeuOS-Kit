/* elifdef.c — C23 #elifdef/#elifndef regression test.
 *
 * Regression: when the prior #if/#ifdef group was not taken, the skipped
 * region was processed by pp.c skipbody(); a #elifdef/#elifndef that
 * evaluated TRUE returned with tok still on the macro name, and the outer
 * directive() then mis-reported "expected newline after preprocessing
 * directive".  Now the rest of the directive line is consumed before the
 * branch becomes active.
 *
 * Each case returns a distinct exit code; exit 0 = all passed.
 */

/* Case 1: prior group not taken (#if 0), #elifdef FOO undefined -> #else */
#if 0
int x = 1;
#elifdef FOO
int x = 2;
#else
int x = 3;
#endif

/* Case 2: prior group not taken, #elifndef BAR (undefined) -> taken */
#if 0
int y = 1;
#elifndef BAR
int y = 2;
#else
int y = 3;
#endif

/* Case 3: prior group not taken, #elifdef BAZ (defined) -> taken */
#define BAZ 1
#if 0
int z = 1;
#elifdef BAZ
int z = 2;
#else
int z = 3;
#endif

/* Case 4: prior group taken (#if 1), later #elifdef must be skipped */
#if 1
int w = 1;
#elifdef BAZ
int w = 2;
#else
int w = 3;
#endif

/* Case 5: chained #elifdef after a not-taken #ifdef */
#ifdef NOPE
int v = 1;
#elifdef NOPE2
int v = 2;
#elifndef NOPE3
int v = 3;
#else
int v = 4;
#endif

/* Case 6: nested conditionals inside a skipped group (not evaluated) */
#if 0
#elifdef WHATEVER
#if 0
#elifdef INNER
#endif
#endif

int
main(void)
{
	if (x != 3)
		return 1;
	if (y != 2)
		return 2;
	if (z != 2)
		return 3;
	if (w != 1)
		return 4;
	if (v != 3)
		return 5;
	return 0;
}
