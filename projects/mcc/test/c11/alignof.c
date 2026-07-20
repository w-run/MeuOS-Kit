extern int puts(const char *);

int
main(void)
{
	if (_Alignof(long long) < 8) {
		puts("FAIL");
		return 1;
	}
	puts("PASS");
	return 0;
}
