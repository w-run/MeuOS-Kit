/* Minimal /etc/group-backed getgrnam/getgrgid.
 * NOT full NSS; flat-file parse, static buffer (see pwd.c rationale).
 */

#include <errno.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 512

static char line[MAX_LINE];

static struct group *
build_entry_name(char *name, char *passwd, gid_t gid, char *members)
{
	static struct group gr;
	static char *memv[32];

	gr.gr_name = name;
	gr.gr_passwd = passwd;
	gr.gr_gid = gid;
	/* Split comma-separated member list into a NULL-terminated array. */
	{
		int n = 0;
		char *p = members;
		gr.gr_mem = memv;
		while (p && *p && n < 31) {
			char *comma = strchr(p, ',');
			if (comma)
				*comma = '\0';
			memv[n++] = p;
			p = comma ? comma + 1 : NULL;
		}
		memv[n] = NULL;
	}
	return &gr;
}

static struct group *
lookup(int by_gid, unsigned long key, const char *name)
{
	FILE *f;

	f = fopen("/etc/group", "re");
	if (!f)
		return NULL;
	while (fgets(line, sizeof line, f)) {
		char *passwd, *gid_s, *members;

		line[strcspn(line, "\n")] = '\0';
		passwd = strchr(line, ':');
		if (!passwd)
			continue;
		*passwd++ = '\0';
		gid_s = strchr(passwd, ':');
		if (!gid_s)
			continue;
		*gid_s++ = '\0';
		members = strchr(gid_s, ':');
		if (members)
			*members++ = '\0';
		if (by_gid ? (strtoul(gid_s, NULL, 10) == key)
		           : (name && strcmp(line, name) == 0)) {
			fclose(f);
			return build_entry_name(line, passwd, (gid_t)strtoul(gid_s, NULL, 10),
			                        members);
		}
	}
	fclose(f);
	return NULL;
}

struct group *
getgrnam(const char *name)
{
	return lookup(0, 0, name);
}

struct group *
getgrgid(gid_t gid)
{
	return lookup(1, (unsigned long)gid, NULL);
}
