/* C23 __VA_OPT__ boundary cases.
 *
 * Covers:
 *  - multiple variadic arguments substituted by __VA_OPT__ content
 *  - content that is itself a full expression, not just a comma or
 *    a parenthesized list
 *  - __VA_OPT__ with empty and single-argument variadic lists
 *  - nested __VA_OPT__ use inside another variadic macro's content
 */
extern int printf(const char *, ...);

/* content is an expression that references __VA_ARGS__ */
#define SUM(...) ((int)sizeof((int[]){ __VA_ARGS__ }) + __VA_OPT__(42))

/* multi-argument: content joins all variadic args with commas via
 * __VA_ARGS__ in the content */
#define LIST(fmt, ...) printf(fmt __VA_OPT__(, __VA_ARGS__))

/* content a full expression referencing __VA_ARGS__; when the variadic
 * list is empty the content is omitted entirely */
#define LAST(...) (1 __VA_OPT__(+ (__VA_ARGS__) * 2))

int main(void) {
	/* SUM(1,2,3) -> sizeof(int[3]) + 42 = 12 + 42 = 54 */
	if (SUM(1, 2, 3) != 54) return 1;
	/* SUM(9) -> sizeof(int[1]) + 42 = 4 + 42 = 46 */
	if (SUM(9) != 46) return 2;
	(void)printf;

	/* multi-arg substitution */
	LIST("a %d %d\n", 1, 2);
	LIST("b %d\n", 3);

	/* content expression referencing __VA_ARGS__ */
	if (LAST(5) != 11) return 3;      /* 1 + 5*2 */
	if (LAST(2, 3) != 7) return 4;     /* 1 + (2,3)*2 -> comma expr = 3 -> 7 */

	return 0;
}
