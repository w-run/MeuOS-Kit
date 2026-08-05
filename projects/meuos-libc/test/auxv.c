#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/auxv.h>

extern const char *__progname;

int
main(int argc, char **argv)
{
	unsigned long pagesz;
	void *phdr;
	unsigned long missing;

	/* __progname must be set from argv[0] by crt1.S before main(). */
	if (argc < 1 || !argv[0] || !__progname)
		return 1;
	if (strcmp(__progname, argv[0]) != 0) {
		printf("FAIL __progname=%s argv[0]=%s\n", __progname, argv[0]);
		return 1;
	}

	/* getauxval must return the kernel-supplied page size (positive). */
	errno = 0;
	pagesz = getauxval(AT_PAGESZ);
	if (pagesz == 0 || pagesz % 4096 != 0) {
		printf("FAIL getauxval(AT_PAGESZ)=%lu errno=%d\n", pagesz, errno);
		return 1;
	}

	/* AT_PHDR is always present for an ELF executable. */
	phdr = (void *)getauxval(AT_PHDR);
	if (!phdr) {
		puts("FAIL getauxval(AT_PHDR)=NULL");
		return 1;
	}

	/* A type not present in the auxv must yield 0 with errno=ENOENT. */
	errno = 0;
	missing = getauxval(AT_EXECFD);
	if (missing != 0 || errno != ENOENT) {
		printf("FAIL missing type: val=%lu errno=%d\n", missing, errno);
		return 1;
	}

	puts("PASS auxv entry contract");
	return 0;
}
