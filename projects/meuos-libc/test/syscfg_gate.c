/* syscfg_gate.c — system-configuration fine-grained gate (uname/confstr/
 * pathconf/sysconf), covering functions that previously had no dedicated
 * gate at all. */
#include <sys/utsname.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static int fails;

static void
chk(const char *lbl, int cond)
{
	if (!cond) {
		printf("FAIL: %s (errno=%d %s)\n", lbl, errno, "err");
		fails++;
	}
}

static void
chk_str_nonempty(const char *lbl, const char *s)
{
	if (!s || !s[0]) {
		printf("FAIL: %s empty\n", lbl);
		fails++;
	}
}

int
main(void)
{
	struct utsname u;

	/* uname(): all fields populated and non-empty */
	errno = 0;
	chk("uname", uname(&u) == 0);
	chk_str_nonempty("uname.sysname", u.sysname);
	chk_str_nonempty("uname.nodename", u.nodename);
	chk_str_nonempty("uname.release", u.release);
	chk_str_nonempty("uname.machine", u.machine);

	/* confstr(_CS_PATH): POSIX-required, returns a path string */
	errno = 0;
	{
		char buf[256];
		size_t n = confstr(_CS_PATH, buf, sizeof buf);
		chk("confstr(_CS_PATH) nonzero", n > 0);
		if (n > 0) chk_str_nonempty("confstr path content", buf);
	}

	/* confstr(_CS_GNU_LIBC_VERSION): the MeuOS ABI tag */
	{
		char buf[64];
		size_t n = confstr(_CS_GNU_LIBC_VERSION, buf, sizeof buf);
		chk("confstr libc tag nonzero", n > 0);
		if (n > 0) chk("confstr libc tag value", strcmp(buf, "meuos-libc") == 0);
	}

	/* confstr with invalid name -> 0 with EINVAL (as documented) */
	errno = 0;
	{
		char buf[8];
		size_t n = confstr(9999, buf, sizeof buf);
		chk("confstr(badname) returns 0", n == 0);
		chk("confstr(badname) EINVAL", errno == EINVAL);
	}

	/* pathconf on an existing file reports -1 conservatively OR a value;
	 * here we only assert it does not crash and errno stays sane, i.e.
	 * treat -1 with errno set as "value not supported" but never a bogus
	 * return.  We use the current directory via a real fd. */
	errno = 0;
	{
		long v = pathconf(".", _PC_NAME_MAX);
		/* NAME_MAX is either a positive constant or -1 (unsupported). */
		if (v != -1) chk("pathconf NAME_MAX >= 1", v >= 1);
	}

	/* sysconf(_SC_PAGESIZE): positive on Linux */
	errno = 0;
	{
		long v = sysconf(_SC_PAGESIZE);
		chk("sysconf PAGESIZE >= 1", v >= 1);
	}

	if (fails) {
		printf("%d syscfg FAIL\n", fails);
		return 1;
	}
	printf("PASS syscfg\n");
	return 0;
}
