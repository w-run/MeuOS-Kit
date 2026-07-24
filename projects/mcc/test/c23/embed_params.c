/* C23 #embed parameter combinations:
 *   limit(N), prefix(...), suffix(...), if_empty(...)
 * Parameters may be combined in any order and must all take effect. */
int main(void) {
	static const unsigned char a[] = {
#embed "embed.dat" limit(2) prefix(0xAA,) suffix(0xBB,)
	};
	static const unsigned char b[] = {
#embed "embed.dat" prefix(0x01,) limit(1) suffix(0x02,)
	};
	static const unsigned char c[] = {
#embed "embed.dat" limit(3)
	};
	/* if_empty is used only when the resource is empty */
	static const unsigned char d[] = {
#embed "embed_empty.dat" if_empty(0x11, 0x22, 0x33)
	};
	static const unsigned char e[] = {
#embed "embed.dat" limit(0) if_empty(0x44,)
	};
	/* non-empty resource must ignore if_empty */
	static const unsigned char f[] = {
#embed "embed.dat" prefix(0xAA,) if_empty(0x99,) suffix(0xBB,)
	};

	if (sizeof(a) != 4) return 1;
	if (a[0] != 0xAA || a[1] != 0xde || a[2] != 0xad || a[3] != 0xBB) return 2;
	if (sizeof(b) != 3) return 3;
	if (b[0] != 0x01 || b[1] != 0xde || b[2] != 0x02) return 4;
	if (sizeof(c) != 3) return 5;
	if (c[0] != 0xde || c[1] != 0xad || c[2] != 0xbe) return 6;
	if (sizeof(d) != 3) return 7;
	if (d[0] != 0x11 || d[1] != 0x22 || d[2] != 0x33) return 8;
	if (sizeof(e) != 1) return 9;
	if (e[0] != 0x44) return 10;
	if (sizeof(f) != 6) return 11;
	if (f[0] != 0xAA || f[1] != 0xde || f[2] != 0xad || f[3] != 0xbe || f[4] != 0xef || f[5] != 0xBB) return 12;
	return 0;
}
