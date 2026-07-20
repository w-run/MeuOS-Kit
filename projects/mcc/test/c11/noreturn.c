extern int puts(const char *);
extern void exit(int);

_Noreturn void
finish(void)
{
	puts("PASS");
	exit(0);
}

int
main(void)
{
	finish();
}
