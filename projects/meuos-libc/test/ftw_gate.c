/* ftw_gate.c — nftw/<ftw.h> regression gate.
 * Builds a small tree: base/{a(file), d(dir)/b(file), broken(dangling symlink)}.
 * Walks it with nftw (no FTW_PHYS) and asserts the correct per-flag visit
 * counts (FTW_D dirs, FTW_F files, FTW_SLN dangling symlink), and that a
 * missing initial path is reported to the callback as FTW_NS. */
#include <ftw.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

static int flag_f, flag_d, flag_sln, flag_ns;

static int
count_cb(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
	(void)path; (void)st; (void)ftw;
	if (flag == FTW_F) flag_f++;
	else if (flag == FTW_D) flag_d++;
	else if (flag == FTW_SLN) flag_sln++;
	else if (flag == FTW_NS) flag_ns++;
	return 0;
}

int
main(void)
{
	char base[] = "/tmp/ftw_gate.XXXXXX";
	if (!mkdtemp(base)) return 1;

	char pa[256], pd[256], pb[256], psl[256];
	snprintf(pa, sizeof pa, "%s/a", base);
	snprintf(pd, sizeof pd, "%s/d", base);
	snprintf(pb, sizeof pb, "%s/d/b", base);
	snprintf(psl, sizeof psl, "%s/broken", base);
	if (open(pa, O_CREAT|O_WRONLY|O_TRUNC, 0644) < 0) return 1;
	if (mkdir(pd, 0755) != 0) return 1;
	if (open(pb, O_CREAT|O_WRONLY|O_TRUNC, 0644) < 0) return 1;
	if (symlink("nope", psl) != 0) return 1;

	int fails = 0;

	/* walk without FTW_PHYS: dangling symlink -> FTW_SLN */
	flag_f = flag_d = flag_sln = flag_ns = 0;
	if (nftw(base, count_cb, 0, 0) != 0) {
		printf("FAIL: nftw error\n");
		return 1;
	}
	if (flag_d != 2) { printf("FAIL: dirs=%d want 2\n", flag_d); fails++; }
	if (flag_f != 2) { printf("FAIL: files=%d want 2\n", flag_f); fails++; }
	if (flag_sln != 1) { printf("FAIL: dangling-symlinks=%d want 1\n", flag_sln); fails++; }

	/* walk a missing initial path: POSIX delivers FTW_NS to the callback
	 * (and does not error out), because the callback decides the outcome. */
	flag_ns = 0;
	{
		char nothere[300];
		snprintf(nothere, sizeof nothere, "%s/nope/missing", base);
		if (nftw(nothere, count_cb, 0, 0) != 0) {
			printf("FAIL: nftw(missing) should not error\n");
			fails++;
		} else if (flag_ns != 1) {
			printf("FAIL: missing path did not deliver FTW_NS (ns=%d)\n", flag_ns);
			fails++;
		}
	}

	if (fails) {
		printf("%d ftw FAIL\n", fails);
		return 1;
	}
	printf("PASS ftw\n");

	unlink(psl); unlink(pb); unlink(pa); rmdir(pd); rmdir(base);
	return 0;
}
