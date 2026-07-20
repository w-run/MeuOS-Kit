extern int puts(const char *);
struct pair { int first, second; };

int
main(void)
{
	struct pair p = {.second = 2, .first = 1};
	if (p.first != 1 || p.second != 2) {
		puts("FAIL");
		return 1;
	}
	puts("PASS");
	return 0;
}
