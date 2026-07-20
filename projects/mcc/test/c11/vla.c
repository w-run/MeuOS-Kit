extern int puts(const char *);

int
main(void)
{
	int i, total = 0, n = 5;
	int values[n];
	for (i = 0; i < n; ++i) {
		values[i] = i;
		total += values[i];
	}
	if (total != 10) {
		puts("FAIL");
		return 1;
	}
	puts("PASS");
	return 0;
}
