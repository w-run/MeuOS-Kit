/* Test C23 #embed directive */
int main(void) {
	static const unsigned char data[] = {
		#embed "embed.dat"
	};
	if (sizeof(data) != 4) return 1;
	if (data[0] != 0xde) return 2;
	if (data[1] != 0xad) return 3;
	if (data[2] != 0xbe) return 4;
	if (data[3] != 0xef) return 5;
	return 0;
}
