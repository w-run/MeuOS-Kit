/* enum_class.cc — C++11 scoped enumerations (`enum class`).
 *
 * A scoped enumeration:
 *   - does NOT implicitly convert to its underlying type or any integer
 *     type (an explicit `(int)c` cast is required);
 *   - names its enumerators via the qualified `Color::Red` form (they
 *     live in the enum's own scope, not the enclosing scope);
 *   - may carry an explicit underlying type (`enum class E : unsigned
 *     char`), which fixes the size/representation;
 *   - coexists with ordinary (unscoped) enumerations.
 *
 * Returns 0 on success.
 */

enum class Color { Red, Green, Blue };
enum class Week : unsigned char { Mon = 1, Tue, Wed };

int
main(void)
{
	/* scoped access via `::` */
	Color c = Color::Green;
	if ((int)c != 1) return 1;       /* explicit cast to int */
	if (c == Color::Red) return 2;
	if (c != Color::Green) return 3;

	/* explicit underlying type fixes the representation */
	if (sizeof(Week) != 1) return 4;
	Week w = Week::Tue;
	if ((unsigned)w != 2) return 5;
	if (w == Week::Wed) return 6;
	if (w != Week::Tue) return 7;

	/* ordinary unscoped enum still works (implicit int) */
	enum Suit { Hearts, Spades, Clubs, Diamonds };
	enum Suit s = Spades;
	if (s != 1) return 8;
	if (Hearts + 2 != 2) return 9;

	return 0;
}
