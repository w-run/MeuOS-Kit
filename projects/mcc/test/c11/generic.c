extern int puts(const char *);
#define IS_INT(x) _Generic((x), int: 1, default: 0)

int
main(void)
{
	int x = 1;
	long y = 1;
	if (IS_INT(x) != 1 || IS_INT(y) != 0) {
		puts("FAIL");
		return 1;
	}
	puts("PASS");
	return 0;
}
