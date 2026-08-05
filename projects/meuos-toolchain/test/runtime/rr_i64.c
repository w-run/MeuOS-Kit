/* runtime-matrix: 64-bit shift. expect exit 42. */
int main(void) {
	long long x = (long long)1 << 40;
	return (x >> 32) == 256 ? 42 : 0;
}
