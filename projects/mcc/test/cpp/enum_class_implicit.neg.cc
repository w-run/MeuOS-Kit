/* enum_class_implicit.neg.cc — C++11 scoped enum must NOT implicitly
 * convert to an integer type (only an explicit cast is allowed).
 *
 * A scoped enumeration does not undergo implicit conversion to its
 * underlying type or to any integral type.  Both the assignment and the
 * comparison against an integer below are ill-formed and must be
 * rejected by m++.
 */

enum class Color { Red, Green, Blue };

int
main(void)
{
	Color c = Color::Green;
	int x = c;          /* REJECT: no implicit enum -> int */
	if (c == 1)         /* REJECT: scoped enum vs int comparison */
		return 1;
	return x;
}
