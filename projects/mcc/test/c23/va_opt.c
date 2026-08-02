/* C23 __VA_OPT__: variadic macro content substituted only when the
 * variadic argument is non-empty.
 *
 *   LOG("hi")       -> printf("hi")
 *   LOG("%d", 42)   -> printf("%d", 42)
 */
#define LOG(fmt, ...) printf(fmt __VA_OPT__(,) __VA_ARGS__)

/* content may itself reference __VA_ARGS__ */
#define PAIR(a, ...) ((a) __VA_OPT__(+ (__VA_ARGS__)))

/* empty variadic arg forwarded through another variadic macro */
#define FWD(fmt, ...) LOG(fmt, __VA_ARGS__)

extern int printf(const char *, ...);

int main(void) {
	LOG("no args\n");
	LOG("one %d\n", 1);
	LOG("two %d %d\n", 2, 3);

	if (PAIR(5) != 5) return 1;          /* __VA_OPT__ empty */
	if (PAIR(1, 2) != 3) return 2;       /* non-empty: (1) + (2) */

	FWD("fwd\n");
	FWD("fwd %d\n", 9);

	return 0;
}
