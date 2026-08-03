/* C 6.10.4: the #line directive sets the presumed line number of the
 * *following* source line.  After `#line 100`, the next line's __LINE__
 * must be exactly 100 (not 99 and not 101), and it must keep counting up
 * from there.
 *
 * This guards an off-by-one regression: the scanner keeps a 0-based line
 * counter and __LINE__ reports counter+1, so #line must store n-1 rather
 * than n.
 */

int main(void) {
	/* baseline: __LINE__ without any #line directive in effect.  This
	 * literal must match the physical line number it appears on. */
	if (__LINE__ != 14)
		return 1;

#line 100
	if (__LINE__ != 100)
		return 2;
	if (__LINE__ != 102)   /* two lines further on */
		return 3;

	/* #line with a filename operand behaves the same way */
#line 200 "renamed.c"
	if (__LINE__ != 200)
		return 4;

	/* the presumed number keeps advancing across ordinary lines */
	if (__LINE__ != 204)
		return 5;

	/* a later #line resets the count again */
#line 50
	if (__LINE__ != 50)
		return 6;

	/* __LINE__ inside a macro argument uses the invocation line */
#line 300
#define ID(x) (x)
	if (ID(__LINE__) != 301)
		return 7;

	return 0;
}
