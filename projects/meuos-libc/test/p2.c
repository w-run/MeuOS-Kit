/* P2 gap batch gate: confstr, strsignal, htonl/htons, getpwnam/getgrnam. */
#include <grp.h>
#include <netinet/in.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int
main(void)
{
	/* confstr(_CS_PATH) must return the length and a path string. */
	{
		char buf[64];
		size_t n = confstr(_CS_PATH, buf, sizeof buf);
		if (n == 0 || n > sizeof buf)
			return 10;
		if (strstr(buf, "bin") == 0)
			return 11;
	}

	/* strsignal must be non-NULL and describe a known signal. */
	if (!strsignal(SIGTERM) || !strsignal(SIGKILL))
		return 20;
	if (!strsignal(1))          /* any valid signal */
		return 21;

	/* byte order: known big-endian patterns. */
	if (htonl(0x01020304u) != 0x04030201u)
		return 30;
	if (htons(0x0102u) != 0x0201u)
		return 31;
	if (ntohl(htonl(0xdeadbeefu)) != 0xdeadbeefu)
		return 32;
	if (ntohs(htons(0xabcd)) != 0xabcd)
		return 33;

	/* passwd/group: if /etc/passwd exists, root must resolve to uid 0.  The
	 * checks are environment-tolerant (SKIP, not FAIL, when no file). */
	{
		struct passwd *pw = getpwnam("root");
		if (pw) {
			if (pw->pw_uid != 0)
				return 40;
			if (getpwuid(0) == NULL)
				return 41;
		}
		struct group *gr = getgrnam("root");
		if (gr) {
			if (gr->gr_gid != 0)
				return 42;
			if (getgrgid(0) == NULL)
				return 43;
		}
	}

	puts("PASS p2");
	return 0;
}
