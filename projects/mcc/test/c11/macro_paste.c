int puts(const char *);

#define JOIN(left, right) left ## right
#define UNIQUE(name) mcc_ ## name

int
main(void)
{
	int JOIN(va, lue) = 7;
	int UNIQUE(token) = value;

	if (mcc_token != 7)
		return 1;
	puts("PASS");
	return 0;
}
