/* Test #embed limit(2) */
int main(void) {
	static const unsigned char data[] = {
#embed "embed.dat" limit(2)
	};
	if (sizeof(data) != 2) return 1;
	return 0;
}
