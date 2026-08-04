/* default_tmpl_arg.cc — C++11 default template arguments.
 *
 * A template parameter may carry a default argument, used when the
 * caller omits it:
 *   - type parameter:  `template<typename T = int>` — `X<>` uses int,
 *     `X<double>` uses double;
 *   - non-type (value):`template<int N = 5>` — `get<>()` yields 5,
 *     `get<7>()` yields 7;
 *   - function templates deduce the type when possible, but fall back to
 *     the default when the argument is omitted.
 *
 * Returns 0 on success.
 */

template <typename T = int>
T
id(T x)
{
	return x;
}

template <typename T = int>
T
make(void)
{
	return (T)42;
}

template <int N = 5>
int
get(void)
{
	return N;
}

template <typename T = int>
struct Box {
	T val;
};

int
main(void)
{
	/* function template: explicit type overrides, else deduce */
	if (id(5) != 5) return 1;             /* deduct T=int */
	if (id<double>(2.5) != 2.5) return 2; /* explicit T=double */

	/* function template with no argument to deduce from: uses default */
	if (make<>() != 42) return 3;          /* default int */
	if (make<double>() != 42.0) return 4;  /* explicit double */

	/* non-type (value) default */
	if (get<>() != 5) return 5;            /* default N=5 */
	if (get<7>() != 7) return 6;           /* explicit N=7 */

	/* class template: empty `<>` and explicit type */
	Box<> b;
	b.val = 7;
	if (b.val != 7) return 7;
	Box<double> bd;
	bd.val = 2.5;
	if (bd.val != 2.5) return 8;

	return 0;
}
