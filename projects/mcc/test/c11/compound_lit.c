extern int puts(const char *);

int
main(void)
{
	int *items = (int[]){1, 2, 3};
	if (items[2] != 3) {
		puts("FAIL");
		return 1;
	}
	puts("PASS");
	return 0;
}
