/* Test #embed prefix/suffix */
int main(void) {
	static const unsigned char data[] = {
#embed "embed.dat" prefix(0x10,) suffix(0x20,)
	};
	if (sizeof(data) != 6) return 1;
	if (data[0] != 0x10) return 2;
	if (data[4] != 0xef) return 3;
	if (data[5] != 0x20) return 4;
	return 0;
}
