/* C23: bool / true / false are reserved keywords (6.4.1, 7.19) and are
 * usable without <stdbool.h>.  This complements the C11 <stdbool.h> test
 * by exercising the keyword spelling directly.
 */
int main(void) {
	bool a = true;
	bool b = false;
	if (a != 1) return 1;
	if (b != 0) return 2;

	bool c = !a;          /* !true == false */
	if (c) return 3;

	bool d = (a && !b);   /* true */
	if (!d) return 4;

	/* bool promotes to int in arithmetic */
	int s = a + b + a;    /* 1 + 0 + 1 = 2 */
	if (s != 2) return 5;

	/* relational/equality on bool */
	if (!(a > b)) return 6;
	if (true == 1) { } else return 7;
	if (false == 0) { } else return 8;

	return 0;
}
