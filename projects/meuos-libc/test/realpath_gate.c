/* realpath_gate.c — realpath regression gate.
 * Builds a real dir tree, then verifies realpath canonicalizes '.'/'..'/'//',
 * resolves relative inputs against the cwd, follows a symlink, rejects a
 * missing path (NULL), and returns a malloc'd result for resolved==NULL. */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

static int fails;

static void
check(const char *label, const char *got, const char *want)
{
	if (!got || strcmp(got, want) != 0) {
		printf("FAIL: %s = '%s' want '%s'\n", label, got ? got : "NULL", want);
		fails++;
	} else {
		/* printf("ok %s: %s\n", label, got); */
	}
}

int
main(void)
{
	char base[] = "/tmp/realpath_gate.XXXXXX";
	if (!mkdtemp(base)) return 1;

	char ad[256], subd[256], af[256], link[256];
	snprintf(ad, sizeof ad, "%s/a", base);
	snprintf(subd, sizeof subd, "%s/a/b", base);
	snprintf(af, sizeof af, "%s/a/b/f", base);
	snprintf(link, sizeof link, "%s/link", base);
	if (mkdir(ad, 0755) != 0) return 1;
	if (mkdir(subd, 0755) != 0) return 1;
	if (open(af, O_CREAT|O_WRONLY|O_TRUNC, 0644) < 0) return 1;
	if (symlink(af, link) != 0) return 1;

	char buf[4096];
	char *r;

	/* absolute with ./ and .. collapse */
	r = realpath(af, buf);
	check("a/b/f", r, af);

	/* '.' inside */
	{
		char p[300];
		snprintf(p, sizeof p, "%s/./a//b/f", base);
		r = realpath(p, buf);
		check("./ + //", r, af);
	}

	/* '..' */
	{
		char p[300];
		snprintf(p, sizeof p, "%s/a/../a/b/f", base);
		r = realpath(p, buf);
		check("..", r, af);
	}

	/* relative against cwd */
	if (chdir(subd) != 0) { perror("chdir"); return 1; }
	r = realpath("f", buf);
	check("relative f", r, af);

	/* symlink follows to target file */
	r = realpath(link, buf);
	check("symlink", r, af);

	/* missing path -> NULL */
	r = realpath("/tmp/realpath_gate_nonexistent_zz", buf);
	if (r != NULL) { printf("FAIL: missing path should be NULL\n"); fails++; }

	/* resolved==NULL -> malloc'd result */
	r = realpath(af, NULL);
	check("malloc", r, af);
	if (r) free(r);

	if (fails) {
		printf("%d realpath FAIL\n", fails);
		return 1;
	}
	printf("PASS realpath\n");

	unlink(link); unlink(af); rmdir(subd); rmdir(ad); rmdir(base);
	return 0;
}
