/* p0_ctype_tables.c — P0 ctype 平表合约 gate.
 *
 * Validates both flavours of the ctype lookup tables:
 *   - the function-pointer form (__ctype_b_loc / __ctype_tolower_loc /
 *     __ctype_toupper_loc), which is what glibc-style code uses
 *     (perl/python/qemu); and
 *   - the flat-array form (__ctype_b / __ctype_tolower /
 *     __ctype_toupper), the musl/BSD route that lets foreign code
 *     index the table directly without going through the function.
 *
 * Both views are required to agree byte-for-byte, and the upper-half
 * entries (index 128..383) must reflect the documented _IS* bit
 * definitions and the documented tolower/toupper mapping.
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* Public flat-array ctype tables (musl/BSD-style).  Defined in
 * src/ctype/ctype.c so the same backing storage feeds the
 * __ctype_b_loc() function-pointer form.  Declared with the exact
 * type foreign code expects so a hostile ABI mismatch would surface
 * here at compile time, not at runtime. */
extern unsigned short *__ctype_b;
extern int *__ctype_tolower;
extern int *__ctype_toupper;

#define OFF 128  /* (unsigned char)c + 128 */

static int
check_is(unsigned short flags, unsigned short want)
{
	/* _IS* bits that musl/glibc consider interesting on a per-character
	 * basis — we test a curated subset so a regression in either
	 * direction (flag set that should not be, or flag missing that
	 * should be present) trips the gate. */
	const unsigned short mask = _ISupper | _ISlower | _ISalpha
	                          | _ISdigit | _ISxdigit | _ISspace
	                          | _ISprint | _ISgraph | _ISblank
	                          | _IScntrl | _ISpunct | _ISalnum;
	return (flags & mask) != (want & mask);
}

int
main(void)
{
	const unsigned short *bp;
	const int *lp, *up;
	int i, err = 0;

	/* Function-pointer flavour first. */
	bp = *__ctype_b_loc();
	lp = *__ctype_tolower_loc();
	up = *__ctype_toupper_loc();
	if (!bp || !lp || !up) {
		puts("FAIL ctype loc NULL");
		return 1;
	}

	/* Spot-check 'A' (uppercase letter, xdigit). */
	if (check_is(bp['A' + 0], _ISupper | _ISalpha | _ISxdigit | _ISprint | _ISgraph | _ISalnum))
		{ puts("FAIL 'A' bits in b_loc"); err = 1; }
	if (lp['A'] != 'a') { puts("FAIL tolower('A') b_loc"); err = 1; }
	if (up['a'] != 'A') { puts("FAIL toupper('a') b_loc"); err = 1; }

	/* Spot-check '5' (digit + xdigit). */
	if (check_is(bp['5' + 0], _ISdigit | _ISxdigit | _ISprint | _ISgraph | _ISalnum))
		{ puts("FAIL '5' bits in b_loc"); err = 1; }

	/* Spot-check ' ' (space + blank + print). */
	if (check_is(bp[' ' + 0], _ISspace | _ISblank | _ISprint))
		{ puts("FAIL ' ' bits in b_loc"); err = 1; }
	if (lp[' '] != ' ' || up[' '] != ' ') {
		puts("FAIL space stays space"); err = 1;
	}

	/* Spot-check a control character: '\t' is space+blank, not print. */
	if (check_is(bp['\t' + 0], _ISspace | _ISblank | _IScntrl))
		{ puts("FAIL '\\t' bits in b_loc"); err = 1; }

	/* Spot-check a punctuation character: '!' is graph+punct+print. */
	if (check_is(bp['!' + 0], _ISprint | _ISgraph | _ISpunct))
		{ puts("FAIL '!' bits in b_loc"); err = 1; }
	if (lp['!'] != '!' || up['!'] != '!') {
		puts("FAIL '!' stays '!'"); err = 1;
	}

	/* Now the flat-array flavour.  The arrays must agree with the
	 * function-pointer view byte-for-byte, otherwise a foreign program
	 * that calls the function and one that indexes the array would see
	 * different ctype classifications of the same character. */
	if (!__ctype_b || !__ctype_tolower || !__ctype_toupper) {
		puts("FAIL flat-array ctype NULL"); return 1;
	}
	for (i = 0; i < 256; ++i) {
		/* bp/lp/up already point at the +128-offset half of the
		 * table, so bp[i] == ctype_b_tab[128+i]; __ctype_b points
		 * at the base of the same table so __ctype_b[i+OFF] is
		 * the same slot. */
		if (__ctype_b[i + OFF] != bp[i]) {
			printf("FAIL flat b[%d]=%x loc=%x\n",
			    i, __ctype_b[i + OFF], bp[i]);
			err = 1;
			break;
		}
		if (__ctype_tolower[i + OFF] != lp[i]) {
			printf("FAIL flat tolower[%d]=%d loc=%d\n",
			    i, __ctype_tolower[i + OFF], lp[i]);
			err = 1;
			break;
		}
		if (__ctype_toupper[i + OFF] != up[i]) {
			printf("FAIL flat toupper[%d]=%d loc=%d\n",
			    i, __ctype_toupper[i + OFF], up[i]);
			err = 1;
			break;
		}
	}

	if (err) return 1;
	puts("PASS ctype 平表 (loc + flat-array)");
	return 0;
}