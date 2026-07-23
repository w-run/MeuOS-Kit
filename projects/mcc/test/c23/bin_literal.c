/* Test C23 0b binary literals and ' digit separators */
int main(void) {
	/* binary literal */
	int a = 0b11010010;      /* 0xD2 = 210 */
	int b = 0B00101111;      /* 0x2F = 47  */

	/* digit separator */
	long c = 1'000'000;      /* 1000000 */
	int d = 0xFF'00;         /* 0xFF00 = 65280 */
	int e = 0b1101'0010;    /* 0xD2 = 210  */

	if (a != 210) return 1;
	if (b != 47)  return 2;
	if (c != 1000000) return 3;
	if (d != 65280) return 4;
	if (e != 210) return 5;

	return 0;
}
