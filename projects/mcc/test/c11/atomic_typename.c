/* Test _Atomic(type-name) syntax (C11 6.7.2.4) */
int main(void) {
	_Atomic(int) x = 0;
	_Atomic(int) y = 42;
	x = y;
	x += 1;
	int z = x;
	return (int)(z == 43 ? 0 : 1);
}
