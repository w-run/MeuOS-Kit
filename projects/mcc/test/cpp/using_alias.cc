/* C++11 alias declaration `using Name = Type;` — including template
 * type aliases `template<...> using Name = Type;` (baseline support that
 * P2360 init-statement aliases build on).  Returns 0 on success. */
using Int = int;
using Pair = struct { int a; int b; };
using CStr = const char *;

template <typename T> using Vec = T;
template <typename T> using Ptr = T *;

int
main(void)
{
	Int x = 5;
	if (x != 5) return 1;

	CStr s = "hi";
	if (s[0] != 'h') return 2;

	Pair p = {1, 2};
	if (p.a + p.b != 3) return 3;

	using Local = unsigned char;
	Local c = 200;
	if (c != 200) return 4;

	/* template type aliases */
	Vec<int> y = 7;
	if (y != 7) return 5;

	Ptr<int> q = 0;
	if (q != 0) return 6;

	return 0;
}
