/* C23 __VA_OPT__ nested usage boundary (6.10.3.5):
 *
 * An outer variadic macro's __VA_OPT__ content is itself a call to another
 * macro whose body also contains __VA_OPT__.  The inner expansion must be
 * re-scanned so the inner __VA_OPT__ is evaluated against the inner
 * variadic argument list.  This checks that mcc re-evaluates __VA_OPT__
 * during the nested rescan rather than only at the outer level.
 */
extern int printf(const char *, ...);

/* inner: __VA_OPT__ emits a leading comma + args */
#define INNER(...) __VA_OPT__(, __VA_ARGS__)
/* outer: emits `1` then, only if it has args, `+ 0` followed by INNER(...) */
#define OUTER(...) (1 __VA_OPT__(+ 0 INNER(__VA_ARGS__)))

int main(void) {
	if (OUTER() != 1) return 1;          /* no args: (1) */
	if (OUTER(5) != 5) return 2;         /* (1 + 0 , 5) -> 5 */
	if (OUTER(2, 3) != 3) return 3;      /* (1 + 0 , 2 , 3) -> 3 */
	(void)printf;
	return 0;
}
