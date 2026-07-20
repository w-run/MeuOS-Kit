#include <setjmp.h>
#include <stdio.h>

static int
deep(jmp_buf jb, int depth)
{
	if (depth == 0)
		longjmp(jb, 42);
	return -1;
}

int
main(void)
{
	jmp_buf jb;
	int r;

	r = setjmp(jb);
	if (r == 0) {
		deep(jb, 0);
		printf("longjmp did not return to setjmp\n");
		return 1;
	}
	if (r != 42) {
		printf("longjmp value wrong: %d\n", r);
		return 2;
	}

	/* longjmp with 0 must become 1 */
	r = setjmp(jb);
	if (r == 0) {
		longjmp(jb, 0);
		printf("longjmp(0) did not return\n");
		return 3;
	}
	if (r != 1) {
		printf("longjmp(0) should yield 1, got %d\n", r);
		return 4;
	}

	puts("setjmp ok");
	return 0;
}
