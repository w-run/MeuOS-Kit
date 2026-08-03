/* C 6.10.3p2: an identifier currently defined as a macro may be redefined
 * by a second #define only if the new definition is "the same" — the
 * replacement lists must have identical spelling and, for function-like
 * macros, identical parameter spelling.  Such a benign redefinition is
 * accepted silently.
 *
 * Whitespace separation is not significant beyond its presence/absence
 * between tokens, so `#define A 1` and `#define A    1` agree.
 *
 * The complementary case — a redefinition with a *different* replacement
 * list, which is a constraint violation — is covered by
 * test/c23/neg/macro_redefine.neg.c.
 */

/* object-like: identical replacement list */
#define OBJ 1
#define OBJ 1

/* object-like: identical apart from inter-token whitespace */
#define SPACED (1 + 2)
#define SPACED (1   +   2)

/* object-like: empty replacement list on both sides */
#define EMPTY
#define EMPTY

/* function-like: identical parameters and replacement list */
#define ADD(a, b) ((a) + (b))
#define ADD(a, b) ((a) + (b))

/* function-like: variadic, identical on both sides */
#define SUM3(x, ...) ((x) + __VA_ARGS__)
#define SUM3(x, ...) ((x) + __VA_ARGS__)

/* a redefinition after #undef may differ freely: #undef removes the
 * definition, so the second #define is not a redefinition at all */
#define CHANGED 1
#undef CHANGED
#define CHANGED 2

int main(void) {
	if (OBJ != 1)
		return 1;
	if (SPACED != 3)
		return 2;
	if (ADD(2, 3) != 5)
		return 3;
	if (SUM3(1, 2) != 3)
		return 4;
	if (CHANGED != 2)
		return 5;

	/* EMPTY expands to nothing; using it must leave the expression intact */
	if (EMPTY OBJ != 1)
		return 6;

	return 0;
}
