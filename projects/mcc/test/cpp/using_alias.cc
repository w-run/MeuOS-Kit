/* C++11 alias declaration `using Name = Type;` (baseline support that
 * P2360 init-statement aliases build on).  Returns 0 on success. */
using Int = int;
using Pair = struct { int a; int b; };
using CStr = const char *;

int main(void) {
    Int x = 5;
    if (x != 5) return 1;

    CStr s = "hi";
    if (s[0] != 'h') return 2;

    Pair p = {1, 2};
    if (p.a + p.b != 3) return 3;

    using Local = unsigned char;
    Local c = 200;
    if (c != 200) return 4;

    return 0;
}
