/*
 * msys_test.c — Unit test for libmsys + mkmsys (end-to-end via system()).
 *
 * 1. Creates a temp directory with known files
 * 2. Calls mkmsys to pack it into a .msys
 * 3. Opens the .msys via libmsys and verifies every entry
 */

#include "mt/msys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Locate mkmsys binary relative to this test.
 *   test/msys_test.c  →  build/mkmsys  */
static const char *find_mkmsys(void)
{
	const char *env = getenv("MKMSYS");
	if (env) return env;

	/* Check ../build/mkmsys (when cwd = project root) */
	static const char *candidates[] = {
		"build/mkmsys",
		"../build/mkmsys",
		"../../build/mkmsys",
		"projects/meuos-sysroot/build/mkmsys",
		NULL
	};
	for (int i = 0; candidates[i]; i++) {
		if (access(candidates[i], X_OK) == 0)
			return candidates[i];
	}
	return "build/mkmsys"; /* fallback, will fail with useful error */
}

static int test_basic(void)
{
	const char *mkmsys = find_mkmsys();
	char tmpdir[] = "/tmp/msys-test-XXXXXX";
	char msysfile[256];
	char cmd[1024];
	int ret = 1;

	if (!mkdtemp(tmpdir)) { perror("mkdtemp"); return 1; }

	/* Create test files */
	const char *files[] = {
		"hello.txt",   "Hello, World!\n",
		"greeting.txt", "Good morning!\n",
		"sub/deep.txt", "Deep file\n",
		"empty.txt",    "",
		NULL
	};

	for (int i = 0; files[i]; i += 2) {
		char path[512];
		snprintf(path, sizeof(path), "%s/%s", tmpdir, files[i]);
		/* Create parent directory */
		char *slash = strrchr(path, '/');
		if (slash && slash != path) {
			*slash = '\0';
			mkdir(path, 0755);
			*slash = '/';
		}
		FILE *fp = fopen(path, "wb");
		if (!fp) { perror(path); goto cleanup; }
		size_t len = strlen(files[i + 1]);
		if (len > 0) fwrite(files[i + 1], 1, len, fp);
		fclose(fp);
	}

	/* Run mkmsys */
	snprintf(msysfile, sizeof(msysfile), "%s/test.msys", tmpdir);
	snprintf(cmd, sizeof(cmd), "%s -o %s %s 2>&1", mkmsys, msysfile, tmpdir);
	if (system(cmd) != 0) {
		fprintf(stderr, "FAIL: mkmsys command failed\n");
		goto cleanup;
	}

	/* Open and verify via libmsys */
	struct msys *m = msys_open(msysfile);
	if (!m) { perror("msys_open"); goto cleanup; }

	for (int i = 0; files[i]; i += 2) {
		const char *name = files[i];
		const char *expected = files[i + 1];
		size_t expected_len = strlen(expected);

		size_t actual_len;
		const void *data = msys_search(m, name, &actual_len);
		if (!data) {
			fprintf(stderr, "FAIL: msys_search(\"%s\") returned NULL\n", name);
			goto close_cleanup;
		}
		if (actual_len != expected_len) {
			fprintf(stderr, "FAIL: \"%s\" len %zu != expected %zu\n",
			        name, actual_len, expected_len);
			goto close_cleanup;
		}
		if (memcmp(data, expected, actual_len) != 0) {
			fprintf(stderr, "FAIL: \"%s\" content mismatch\n", name);
			goto close_cleanup;
		}

		/* Also test msys_read */
		char buf[1024];
		int n = msys_read(m, name, buf, sizeof(buf));
		if (n < 0) {
			fprintf(stderr, "FAIL: msys_read(\"%s\") returned %d\n", name, n);
			goto close_cleanup;
		}
		if ((size_t)n != expected_len) {
			fprintf(stderr, "FAIL: msys_read(\"%s\") len %d != %zu\n",
			        name, n, expected_len);
			goto close_cleanup;
		}
		if (memcmp(buf, expected, n) != 0) {
			fprintf(stderr, "FAIL: msys_read(\"%s\") content mismatch\n", name);
			goto close_cleanup;
		}
	}

	/* Test non-existent entry */
	{
		size_t sz;
		const void *d = msys_search(m, "nonexistent", &sz);
		if (d != NULL) {
			fprintf(stderr, "FAIL: nonexistent entry should return NULL\n");
			goto close_cleanup;
		}
	}

	printf("PASS: all %d entries verified (msys_search, msys_read)\n", 4);

	/* Test msys_count + msys_enumerate */
	{
		uint32_t cnt = msys_count(m);
		if (cnt < 4) {
			fprintf(stderr, "FAIL: msys_count() = %u, expected at least 4\n", (unsigned)cnt);
			goto close_cleanup;
		}
		/* Verify enumerate returns known files */
		int found = 0;
		for (uint32_t i = 0; i < cnt; i++) {
			const char *ename; size_t elen, esize;
			if (msys_enumerate(m, i, &ename, &elen, &esize) < 0)
				continue;
			/* Check each of the known names */
			for (int j = 0; files[j]; j += 2) {
				if (strlen(files[j]) == elen &&
				    memcmp(ename, files[j], elen) == 0) {
					found++;
					break;
				}
			}
		}
		if (found < 4) {
			fprintf(stderr, "FAIL: msys_enumerate found only %d/4 known files\n", found);
			goto close_cleanup;
		}
		/* Boundary: idx == count */
		const char *bname; size_t bnlen, bsize;
		if (msys_enumerate(m, cnt, &bname, &bnlen, &bsize) == 0) {
			fprintf(stderr, "FAIL: msys_enumerate should fail at idx == count\n");
			goto close_cleanup;
		}
		printf("PASS: msys_count/enumerate (%u entries, %d known)\n", (unsigned)cnt, found);
	}

	ret = 0;

close_cleanup:
	msys_close(m);
cleanup:
	{ /* block needed because -Wpedantic forbids decl after label */
		char rmcmd[512];
		snprintf(rmcmd, sizeof(rmcmd), "rm -rf %s", tmpdir);
		system(rmcmd);
	}
	return ret;
}

int main(void)
{
	int ret = test_basic();
	if (ret == 0)
		printf("msys_test: ALL PASS\n");
	else
		fprintf(stderr, "msys_test: FAILED\n");
	return ret;
}
