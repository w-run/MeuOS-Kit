_Static_assert(sizeof(long long) >= 8, "long long must be at least 64 bits");
extern int puts(const char *);

int
main(void)
{
	puts("PASS");
	return 0;
}
