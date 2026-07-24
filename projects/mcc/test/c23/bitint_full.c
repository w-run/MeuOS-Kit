/* C23: _BitInt(N) precise-width integer types. */
unsigned _BitInt(8) f0(unsigned _BitInt(8) x) { return x + 1; }
_BitInt(8)           f1(_BitInt(7) x)         { return x + 1; }
unsigned _BitInt(9)  f2(void)                 { return 100u; }

int main(void) {
	_BitInt(31)        a = 5;
	_BitInt(64)        b = a + 1;
	unsigned _BitInt(33) c = 1u;
	unsigned _BitInt(8)  d = 0xFF;
	_BitInt(8)          e = -5;

	if (b != 6) return 1;
	if (c != 1) return 2;
	if (f1(10) != 11) return 3;
	if (f2() != 100) return 4;
	if (sizeof(_BitInt(40)) != 8) return 5;
	if (d != 0xFF) return 6;
	if (e != -5) return 7;
	if (a * 3 != 15) return 8;
	if (f0(0xFE) != 0xFF) return 9;
	if (a - 10 != -5) return 10;
	return 0;
}
