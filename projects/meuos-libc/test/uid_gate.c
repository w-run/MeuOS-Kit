/* uid_gate.c — setuid/setgid/seteuid/setegid regression gate.
 *
 * Verifies the identity-set calls succeed when re-assigning a process to its
 * own current ids (always legal), that the effective id is not clobbered by
 * seteuid/setegid round-trips, and that a non-root process gets EPERM when
 * trying to take on an id it cannot.  When run as root, the set-to-0 calls
 * are exercised instead of the privilege-denial path. */
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

static int fails;

static void
chk(const char *lbl, int cond)
{
	if (!cond) {
		printf("FAIL: %s (errno=%d)\n", lbl, errno);
		fails++;
	}
}

int
main(void)
{
	uid_t u = getuid(), e = geteuid();
	gid_t g = getgid(), eg = getegid();

	/* seteuid/setegid to the current effective id: always permitted. */
	errno = 0;
	chk("seteuid(geteuid())", seteuid(e) == 0);
	chk("setegid(getegid())", setegid(eg) == 0);
	chk("euid unchanged", geteuid() == e);
	chk("egid unchanged", getegid() == eg);

	/* setuid/setgid to the current real id: permitted for a normal procs. */
	errno = 0;
	chk("setuid(getuid())", setuid(u) == 0);
	chk("setgid(getgid())", setgid(g) == 0);
	chk("uid unchanged", getuid() == u);
	chk("gid unchanged", getgid() == g);

	if (u != 0) {
		/* non-root: dropping/raising to an unpermitted id must fail EPERM */
		errno = 0;
		if (setuid((uid_t)0x7fffffff) != -1) {
			printf("FAIL: non-root setuid(huge) should fail\n");
			fails++;
		} else if (errno != EPERM) {
			printf("FAIL: non-root setuid errno=%d want EPERM\n", errno);
			fails++;
		}
		errno = 0;
		if (setgid((gid_t)0x7fffffff) != -1) {
			printf("FAIL: non-root setgid(huge) should fail\n");
			fails++;
		} else if (errno != EPERM) {
			printf("FAIL: non-root setgid errno=%d want EPERM\n", errno);
			fails++;
		}
	} else {
		/* root: assigning id 0 to itself succeeds and changes nothing */
		errno = 0;
		chk("root setuid(0)", setuid(0) == 0);
		chk("root setgid(0)", setgid(0) == 0);
	}

	if (fails) {
		printf("%d uid FAIL\n", fails);
		return 1;
	}
	printf("PASS uid\n");
	return 0;
}
