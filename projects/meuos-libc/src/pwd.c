/* Minimal /etc/passwd-backed getpwnam/getpwuid.
 *
 * NOT full NSS: reads the flat file once per call and returns the first
 * matching entry in an internal static buffer of fixed capacity.  Sufficient
 * for the common "look up a user by name/uid" case and for autotools
 * configure probes; lacks NSS, hashing and the streaming setpwent() API.
 */

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 512
#define MAX_FIELDS 7

static char line[MAX_LINE];
static char *fields[MAX_FIELDS];
static char mem[512];

static int
parse_passwd_line(void)
{
	char *p;
	int i;

	p = line;
	for (i = 0; i < MAX_FIELDS && *p; ++i) {
		fields[i] = p;
		p = strchr(p, ':');
		if (!p)
			break;
		*p++ = '\0';
	}
	return i;               /* <7 -> malformed line, skip */
}

static struct passwd *
build_entry(void)
{
	static struct passwd pw;
	char *pool = mem;
	size_t rem = sizeof mem;

#define SAVE(s) do { size_t n = strlen(s) + 1; if (n > rem) return NULL; \
	memcpy(pool, s, n); s = pool; pool += n; rem -= n; } while (0)

	pw.pw_name = fields[0];
	pw.pw_passwd = fields[1];
	pw.pw_uid = (uid_t)strtoul(fields[2], NULL, 10);
	pw.pw_gid = (gid_t)strtoul(fields[3], NULL, 10);
	pw.pw_gecos = fields[4];
	pw.pw_dir = fields[5];
	pw.pw_shell = fields[6];
	/* Strings live in the reusable "line" buffer; copy the ones we return so
	 * they stay valid until the next getpw* call. */
	SAVE(pw.pw_name); SAVE(pw.pw_passwd); SAVE(pw.pw_gecos);
	SAVE(pw.pw_dir); SAVE(pw.pw_shell);
	return &pw;
#undef SAVE
}

struct passwd *
getpwnam(const char *name)
{
	FILE *f;

	if (!name)
		return NULL;
	f = fopen("/etc/passwd", "re");
	if (!f)
		return NULL;
	while (fgets(line, sizeof line, f)) {
		if (parse_passwd_line() >= MAX_FIELDS &&
		    strcmp(fields[0], name) == 0) {
			fclose(f);
			return build_entry();
		}
	}
	fclose(f);
	return NULL;
}

struct passwd *
getpwuid(uid_t uid)
{
	FILE *f;

	f = fopen("/etc/passwd", "re");
	if (!f)
		return NULL;
	while (fgets(line, sizeof line, f)) {
		if (parse_passwd_line() >= MAX_FIELDS &&
		    strtoul(fields[2], NULL, 10) == (unsigned long)uid) {
			fclose(f);
			return build_entry();
		}
	}
	fclose(f);
	return NULL;
}
