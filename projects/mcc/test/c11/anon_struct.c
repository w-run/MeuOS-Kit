extern int puts(const char *);
struct outer { struct { int value; }; };

int
main(void)
{
	struct outer object;
	object.value = 1;
	if (object.value != 1) {
		puts("FAIL");
		return 1;
	}
	puts("PASS");
	return 0;
}
