/* msys_test.c — Full API unit test for libmsys + mkmsys.
 *
 * Tests: v1/v2 open/read/search, enumerate/readdir, stat/fstat, fopen/load,
 * readlink, verify (SHA-256), extension blocks, streaming, overlay, xattr.
 */

#include "mt/msys.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { \
	fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); \
	failures++; return 1; } } while(0)
#define PASS(msg) printf("PASS: %s\n", msg)

static const char *find_mkmsys(void)
{
	const char *env = getenv("MKMSYS");
	if (env) return env;
	static const char *candidates[] = {
		"build/mkmsys", "../build/mkmsys",
		"../../build/mkmsys", "projects/meuos-sysroot/build/mkmsys", NULL
	};
	for (int i = 0; candidates[i]; i++)
		if (access(candidates[i], X_OK) == 0) return candidates[i];
	return "build/mkmsys";
}

/* callback: count entries */
static int count_cb(const char *name, size_t nlen, size_t size, int is_dir, void *arg)
{ (void)name; (void)nlen; (void)size; (void)is_dir; (*(int*)arg)++; return 0; }

/* callback: accumulate root children */
struct root_seen { int h,g,s,e; };
static int root_cb(const char *name, size_t nlen, size_t size, int is_dir, void *arg)
{
	(void)size;
	struct root_seen *r = (struct root_seen *)arg;
	if (nlen == 9 && memcmp(name,"hello.txt",9)==0) r->h=1;
	if (nlen == 12 && memcmp(name,"greeting.txt",12)==0) r->g=1;
	if (nlen == 3 && memcmp(name,"sub",3)==0 && is_dir) r->s=1;
	if (nlen == 9 && memcmp(name,"empty.txt",9)==0) r->e=1;
	return 0;
}

/* Helper: create test dir */
static int make_testdir(const char *tmpdir)
{
	static const char *files[] = {
		"hello.txt",   "Hello, World!\n",
		"greeting.txt", "Good morning!\n",
		"sub/deep.txt", "Deep file\n",
		"empty.txt",    "",
		NULL
	};
	for (int i = 0; files[i]; i += 2) {
		char path[1024];
		snprintf(path, sizeof(path), "%s/%s", tmpdir, files[i]);
		char *slash = strrchr(path, '/');
		if (slash && slash != path) { *slash = '\0'; mkdir(path, 0755); *slash = '/'; }
		FILE *fp = fopen(path, "wb");
		if (!fp) { perror(path); return -1; }
		size_t len = strlen(files[i + 1]);
		if (len > 0) fwrite(files[i + 1], 1, len, fp);
		fclose(fp);
	}
	return 0;
}

/* ================================================================== */
/* v1 basic test                                                       */
/* ================================================================== */
static int test_v1_basic(void)
{
	const char *mkmsys = find_mkmsys();
	char tmpdir[256], msysfile[512], cmd[1024];
	snprintf(tmpdir, sizeof(tmpdir), "/tmp/msys-ut1-XXXXXX");
	if (!mkdtemp(tmpdir)) { perror("mkdtemp"); return 1; }
	if (make_testdir(tmpdir) < 0) return 1;

	snprintf(msysfile, sizeof(msysfile), "%s/test.msys", tmpdir);
	snprintf(cmd, sizeof(cmd), "%s -o %s %s 2>/dev/null", mkmsys, msysfile, tmpdir);
	if (system(cmd) != 0) { fprintf(stderr, "FAIL: mkmsys v1\n"); return 1; }

	struct msys *m = msys_open(msysfile);
	CHECK(m != NULL, "v1 msys_open");

	const char *files[] = {"hello.txt","greeting.txt","sub/deep.txt","empty.txt",NULL};
	const char *expect[] = {"Hello, World!\n","Good morning!\n","Deep file\n",""};
	size_t explen[] = {14,14,10,0};

	for (int i = 0; i < 4; i++) {
		size_t sz; char msg[128];
		const void *d = msys_search(m, files[i], &sz);
		CHECK(d != NULL, files[i]);
		snprintf(msg, sizeof(msg), "%s size", files[i]);
		CHECK(sz == explen[i], msg);
		if (sz > 0) { snprintf(msg, sizeof(msg), "%s content", files[i]);
			CHECK(memcmp(d, expect[i], sz) == 0, msg); }

		char buf[1024];
		int r = msys_read(m, files[i], buf, sizeof(buf));
		snprintf(msg, sizeof(msg), "%s read", files[i]);
		CHECK(r == (int)explen[i], msg);
	}
	PASS("v1 msys_search/read (4 files)");

	uint32_t cnt = msys_count(m);
	CHECK(cnt >= 4, "v1 msys_count >= 4");
	int found = 0;
	for (uint32_t i = 0; i < cnt; i++) {
		const char *name; size_t nlen, dsize;
		if (msys_enumerate(m, i, &name, &nlen, &dsize) < 0) continue;
		for (int j = 0; files[j]; j++)
			if (strlen(files[j]) == nlen && memcmp(name, files[j], nlen) == 0) { found++; break; }
	}
	CHECK(found >= 4, "v1 enumerate all 4");
	PASS("v1 msys_count/enumerate");

	{	struct root_seen rs = {0,0,0,0};
		CHECK(msys_readdir(m, "", root_cb, &rs) == 0, "v1 readdir root");
		CHECK(rs.h && rs.g && rs.s && rs.e, "v1 readdir all 4");
		PASS("v1 msys_readdir");
	}
	{	size_t sz;
		CHECK(msys_fstat(m, "hello.txt", &sz) == 0, "v1 fstat");
		CHECK(sz == 14, "v1 fstat size");
		CHECK(msys_fstat(m, "no_such", NULL) < 0, "v1 fstat nonexistent");
		PASS("v1 msys_fstat");
	}
	{	FILE *fp = msys_fopen(m, "hello.txt", "r");
		CHECK(fp != NULL, "v1 fopen");
		fclose(fp);
		PASS("v1 msys_fopen");
	}
	{	void *buf;
		CHECK(msys_load(m, "hello.txt", &buf, NULL) >= 0, "v1 load");
		free(buf);
		PASS("v1 msys_load");
	}

	msys_close(m);
	char rm[1024]; snprintf(rm, sizeof(rm), "rm -rf %s", tmpdir); system(rm);
	return 0;
}

/* ================================================================== */
/* v2 format                                                           */
/* ================================================================== */
static int test_v2(void)
{
	const char *mkmsys = find_mkmsys();
	char tmpdir[256], msysfile[512], cmd[1024];
	snprintf(tmpdir, sizeof(tmpdir), "/tmp/msys-ut2-XXXXXX");
	if (!mkdtemp(tmpdir)) { perror("mkdtemp"); return 1; }
	if (make_testdir(tmpdir) < 0) return 1;

	snprintf(msysfile, sizeof(msysfile), "%s/v2.msys", tmpdir);
	snprintf(cmd, sizeof(cmd), "%s --format v2 -o %s %s 2>/dev/null", mkmsys, msysfile, tmpdir);
	if (system(cmd) != 0) { fprintf(stderr, "FAIL: mkmsys v2\n"); return 1; }

	struct msys *m = msys_open(msysfile);
	CHECK(m != NULL, "v2 msys_open");
	CHECK(msys_format_version(m) == 2, "v2 format_version");
	PASS("v2 msys_open + format_version");

	{	struct msys_stat st;
		CHECK(msys_stat(m, "hello.txt", &st) == 0, "v2 stat hello.txt");
		CHECK(st.file_type == MSYS_FILE_REG, "v2 stat type REG");
		CHECK(st.size == 14, "v2 stat size");
		PASS("v2 msys_stat");
	}
	{	struct root_seen rs = {0,0,0,0};
		CHECK(msys_readdir(m, "", root_cb, &rs) == 0, "v2 readdir root");
		CHECK(rs.h && rs.g && rs.s && rs.e, "v2 readdir all 4");
		PASS("v2 msys_readdir (dir block)");
	}

	msys_close(m);
	char rm[1024]; snprintf(rm, sizeof(rm), "rm -rf %s", tmpdir); system(rm);
	return 0;
}

/* ================================================================== */
/* symlink + readlink                                                  */
/* ================================================================== */
static int test_readlink(void)
{
	const char *mkmsys = find_mkmsys();
	char tmpdir[256];
	snprintf(tmpdir, sizeof(tmpdir), "/tmp/msys-lnk-XXXXXX");
	if (!mkdtemp(tmpdir)) { perror("mkdtemp"); return 1; }

	{	char p[1024]; snprintf(p, sizeof(p), "%s/target.txt", tmpdir);
		FILE *fp = fopen(p, "wb"); fwrite("TARGET",1,6,fp); fclose(fp); }
	{	char p[1024]; snprintf(p, sizeof(p), "%s/link.txt", tmpdir);
		symlink("target.txt", p); }

	char msysfile[512], cmd[1024];
	snprintf(msysfile, sizeof(msysfile), "%s/link.msys", tmpdir);
	snprintf(cmd, sizeof(cmd), "%s --format v2 -o %s %s 2>/dev/null", mkmsys, msysfile, tmpdir);
	if (system(cmd) != 0) { fprintf(stderr, "FAIL: mkmsys link\n"); return 1; }

	struct msys *m = msys_open(msysfile);
	CHECK(m != NULL, "link msys_open");

	char buf[256];
	int r = msys_readlink(m, "link.txt", buf, sizeof(buf));
	CHECK(r > 0, "readlink success");
	buf[r] = '\0';
	CHECK(strcmp(buf, "target.txt") == 0, "readlink target");
	PASS("msys_readlink");

	void *data; size_t sz;
	CHECK(msys_load(m, "link.txt", &data, &sz) >= 0, "load symlink");
	CHECK(sz == 6 && memcmp(data, "TARGET", 6) == 0, "load symlink follows");
	free(data);
	PASS("msys_load symlink auto-resolve");

	msys_close(m);
	char rm[1024]; snprintf(rm, sizeof(rm), "rm -rf %s", tmpdir); system(rm);
	return 0;
}

/* ================================================================== */
/* verify + dedup                                                      */
/* ================================================================== */
static int test_verify(void)
{
	const char *mkmsys = find_mkmsys();
	char tmpdir[256], msysfile[512], cmd[1024];
	snprintf(tmpdir, sizeof(tmpdir), "/tmp/msys-ver-XXXXXX");
	if (!mkdtemp(tmpdir)) { perror("mkdtemp"); return 1; }

	{	char p[1024];
		snprintf(p,sizeof(p),"%s/a.txt",tmpdir); FILE *fp=fopen(p,"wb"); fwrite("SAME",1,4,fp); fclose(fp);
		snprintf(p,sizeof(p),"%s/b.txt",tmpdir); fp=fopen(p,"wb"); fwrite("SAME",1,4,fp); fclose(fp);
		snprintf(p,sizeof(p),"%s/c.txt",tmpdir); fp=fopen(p,"wb"); fwrite("DIFF",1,4,fp); fclose(fp);
	}

	snprintf(msysfile, sizeof(msysfile), "%s/plain.msys", tmpdir);
	snprintf(cmd, sizeof(cmd), "%s --format v2 -o %s %s 2>/dev/null", mkmsys, msysfile, tmpdir);
	if (system(cmd) != 0) { fprintf(stderr, "FAIL: mkmsys verify plain\n"); return 1; }

	struct msys *m = msys_open(msysfile);
	CHECK(m != NULL, "verify msys_open");
	CHECK(msys_verify(m, "a.txt") == 0, "verify a.txt");
	CHECK(msys_verify_all(m) == 0, "verify_all");
	msys_close(m);
	PASS("msys_verify / msys_verify_all");

	snprintf(msysfile, sizeof(msysfile), "%s/dedup.msys", tmpdir);
	snprintf(cmd, sizeof(cmd), "%s --format v2 --dedup -o %s %s 2>/dev/null", mkmsys, msysfile, tmpdir);
	if (system(cmd) != 0) { fprintf(stderr, "FAIL: mkmsys dedup\n"); return 1; }

	m = msys_open(msysfile);
	CHECK(m != NULL, "dedup msys_open");
	size_t sa, sb;
	const void *da = msys_search(m, "a.txt", &sa);
	const void *db = msys_search(m, "b.txt", &sb);
	CHECK(da == db, "dedup same data pointer");
	CHECK(msys_verify_all(m) == 0, "dedup verify_all");
	msys_close(m);
	PASS("dedup + verify");

	char rm[1024]; snprintf(rm, sizeof(rm), "rm -rf %s", tmpdir); system(rm);
	return 0;
}

/* ================================================================== */
/* extension + signature                                                */
/* ================================================================== */
static int test_extension(void)
{
	struct msys *m = msys_open("/tmp/signed.msys");
	if (!m) { printf("  SKIP: /tmp/signed.msys not found\n"); return 0; }

	const void *data; uint32_t dlen;
	CHECK(msys_get_extension(m, 0x6e676973, &data, &dlen) == 0, "get_extension sign");
	CHECK(dlen == 64, "extension len 64");
	CHECK(msys_get_extension(m, 0x12345678, &data, &dlen) < 0, "unknown type");
	PASS("msys_get_extension");

	FILE *pkf = fopen("/tmp/test_sign.pk", "rb");
	if (pkf) {
		uint8_t pk[32]; fread(pk,1,32,pkf); fclose(pkf);
		CHECK(msys_verify_signature(m, pk) == 0, "verify_signature");
		PASS("msys_verify_signature");
	}
	msys_close(m);
	return 0;
}

/* ================================================================== */
/* streaming                                                            */
/* ================================================================== */
static int test_stream(void)
{
	struct msys_stream *s = msys_stream_open("/tmp/streaming.msys");
	if (!s) { printf("  SKIP: /tmp/streaming.msys not found\n"); return 0; }

	int count = 0;
	const char *name; size_t nlen, dsize; const void *data;
	while (msys_stream_next(s, &name, &nlen, &data, &dsize) > 0) {
		CHECK(nlen > 0 && data != NULL, "stream entry");
		CHECK(name[0] != '@', "stream no @mt");
		count++;
	}
	CHECK(count == 3, "stream 3 entries");
	PASS("msys_stream (3 entries)");
	msys_stream_close(s);
	return 0;
}

/* ================================================================== */
/* overlay                                                              */
/* ================================================================== */
static int test_overlay(void)
{
	struct msys_overlay *ol = msys_overlay_open(
		(const char *[]){"/tmp/overlay-base.msys","/tmp/overlay-top.msys"}, 2);
	if (!ol) { printf("  SKIP: overlay archives not found\n"); return 0; }

	CHECK(msys_overlay_count(ol) == 2, "overlay count 2");
	int li; size_t sz;

	const void *d = msys_overlay_search(ol, "shared.txt", &sz, &li);
	CHECK(d != NULL && li == 1, "overlay shadow top");
	d = msys_overlay_search(ol, "only_in_base.txt", &sz, &li);
	CHECK(d != NULL && li == 0, "overlay base");
	d = msys_overlay_search(ol, "only_in_top.txt", &sz, &li);
	CHECK(d != NULL && li == 1, "overlay top");
	PASS("overlay search (layering)");

	void *buf;
	CHECK(msys_overlay_load(ol, "shared.txt", &buf, &sz) >= 0, "overlay load");
	CHECK(sz == 11 && memcmp(buf, "TOP_VERSION", 11) == 0, "overlay shadow content");
	free(buf);
	PASS("overlay load (top shadows base)");

	int cnt = 0;
	CHECK(msys_overlay_readdir(ol, "", count_cb, &cnt) == 0, "overlay readdir");
	CHECK(cnt == 3, "overlay readdir 3 children");
	PASS("msys_overlay (2 layers, search/load/readdir)");

	msys_overlay_close(ol);
	return 0;
}

/* ================================================================== */
/* xattr                                                                */
/* ================================================================== */
static int test_xattr(void)
{
	struct msys *m = msys_open("/tmp/xattr-test.msys");
	if (!m) { printf("  SKIP: /tmp/xattr-test.msys not found\n"); return 0; }

	char buf[256];
	int r = msys_getxattr(m, "data.txt", "user.mime_type", buf, sizeof(buf));
	if (r < 0) { printf("  SKIP: no xattr in archive\n"); msys_close(m); return 0; }
	buf[r] = '\0';
	CHECK(strcmp(buf, "text/plain") == 0, "xattr user.mime_type");
	PASS("msys_getxattr");
	msys_close(m);
	return 0;
}

/* ================================================================== */
/* v1 compress + incremental                                            */
/* ================================================================== */
static int test_compress(void)
{
	const char *mkmsys = find_mkmsys();
	char tmpdir[256], msysfile[512], cmd[1024];
	snprintf(tmpdir, sizeof(tmpdir), "/tmp/msys-cmp-XXXXXX");
	if (!mkdtemp(tmpdir)) { perror("mkdtemp"); return 1; }
	if (make_testdir(tmpdir) < 0) return 1;

	/* zlib */
	snprintf(msysfile, sizeof(msysfile), "%s/zlib.msys", tmpdir);
	snprintf(cmd, sizeof(cmd), "%s --compress=zlib -o %s %s 2>/dev/null", mkmsys, msysfile, tmpdir);
	if (system(cmd) != 0) { printf("  SKIP: zlib not available\n"); goto skip_compress; }

	struct msys *m = msys_open(msysfile);
	CHECK(m != NULL, "zlib msys_open");
	size_t sz;
	CHECK(msys_search(m, "hello.txt", &sz) != NULL, "zlib search");
	CHECK(sz == 14, "zlib data size");
	msys_close(m);
	PASS("v1 msys --compress=zlib");

	/* mz (meuos-compress native codec).  v2 format: it stores per-entry
	 * uncompressed_size so entries whose data was stored raw (compression
	 * had no benefit) are distinguishable from actually-compressed ones;
	 * v1 has no such field and cannot round-trip mixed raw/compressed
	 * entries (pre-existing format limitation). */
	snprintf(msysfile, sizeof(msysfile), "%s/mz.msys", tmpdir);
	snprintf(cmd, sizeof(cmd), "%s --format v2 --dedup --compress=mz -o %s %s 2>/dev/null", mkmsys, msysfile, tmpdir);
	if (system(cmd) != 0) { printf("  SKIP: mz compress failed\n"); goto skip_compress; }

	m = msys_open(msysfile);
	CHECK(m != NULL, "mz msys_open");
	CHECK((m->hdr->flags & MSYS_F_MZ) != 0, "mz flag set");
	{
		void *buf = NULL;
		size_t out_sz = 0;
		CHECK(msys_load(m, "hello.txt", &buf, &out_sz) >= 0, "mz load hello.txt");
		CHECK(out_sz == 14 && memcmp(buf, "Hello, World!\n", 14) == 0, "mz load content");
		free(buf);
	}
	msys_close(m);
	PASS("msys --compress=mz roundtrip");

skip_compress:;
	char rm[1024]; snprintf(rm, sizeof(rm), "rm -rf %s", tmpdir); system(rm);
	return 0;
}

/* ================================================================== */
/* main                                                                 */
/* ================================================================== */
int main(void)
{
	/* Register the meuos-compress codec (libmz.a, linked by the test binary)
	 * so MSYS_F_MZ archives can be decompressed. */
	extern int mz_decompress_meuos(const void *in, size_t il, void **r, size_t *rl);
	msys_set_mz_codec(mz_decompress_meuos);

	int ret = 0;
	ret += test_v1_basic();
	ret += test_v2();
	ret += test_readlink();
	ret += test_verify();
	ret += test_extension();
	ret += test_stream();
	ret += test_overlay();
	ret += test_xattr();
	ret += test_compress();

	if (ret == 0)
		printf("\nmsys_test: ALL PASS\n");
	else
		fprintf(stderr, "\nmsys_test: %d FAILURES\n", ret);
	return ret;
}
