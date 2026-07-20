extern int puts(const char *);
_Alignas(64) static char aligned;

int
main(void)
{
	if (((unsigned long)&aligned & 63) != 0) {
		puts("FAIL");
		return 1;
	}
	puts("PASS");
	return 0;
}
