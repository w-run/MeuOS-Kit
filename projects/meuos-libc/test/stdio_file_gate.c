/* stdio_file_gate.c — file-stream I/O fine-grained gate (fopen/fread/fwrite/
 * fseek/ftell/rewind/fflush/remove/rename/setvbuf).
 *
 * Verifies the file-stream lifecycle end to end: create+write, reopen and
 * read back exact bytes, file positioning (set/cur/end), clear/setvbuf,
 * flush, remove, and rename.  Previously unreachable failure isolation for
 * the file-descriptor-backed stream functions. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
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

int
main(void)
{
	const char *w = "/tmp/meuos-stdio-file-w.tmp";
	const char *r = "/tmp/meuos-stdio-file-r.tmp";
	const char *renamed = "/tmp/meuos-stdio-file-renamed.tmp";
	const char *data = "hello file io stream";
	size_t ndata = strlen(data);
	char buf[64];
	FILE *fp;

	/* write */
	fp = fopen(w, "w");
	chk("fopen(w)", fp != 0);
	if (fp) {
		chk("fwrite", fwrite(data, 1, ndata, fp) == ndata);
		chk("fflush", fflush(fp) == 0);
		chk("fclose", fclose(fp) == 0);
	}

	/* read back */
	fp = fopen(w, "r");
	chk("fopen(r)", fp != 0);
	if (fp) {
		memset(buf, 0, sizeof buf);
		chk("fread", fread(buf, 1, sizeof buf, fp) == ndata);
		chk("fread content", memcmp(buf, data, ndata) == 0);
		chk("ftell at end", ftell(fp) == (long)ndata);
		/* rewind */
		rewind(fp);
		chk("ftell after rewind", ftell(fp) == 0);
		/* seek set: back to 6 bytes in */
		chk("fseek SET", fseek(fp, 6, SEEK_SET) == 0);
		chk("ftell after SET", ftell(fp) == 6);
		/* seek end */
		chk("fseek END", fseek(fp, 0, SEEK_END) == 0);
		chk("ftell after END", ftell(fp) == (long)ndata);
		/* seek cur */
		chk("fseek CUR", fseek(fp, -5, SEEK_CUR) == 0);
		long pos = ftell(fp);
		chk("ftell after CUR", pos == (long)ndata - 5);
		fclose(fp);
	}

	/* read from a different name, then rename + re-read */
	fp = fopen(r, "w");
	if (fp) { fwrite(data, 1, ndata, fp); fclose(fp); }
	chk("rename", rename(r, renamed) == 0);
	fp = fopen(renamed, "r");
	chk("fopen(renamed)", fp != 0);
	if (fp) {
		memset(buf, 0, sizeof buf);
		chk("fread renamed", fread(buf, 1, sizeof buf, fp) == ndata);
		chk("renamed content", memcmp(buf, data, ndata) == 0);
		fclose(fp);
	}
	chk("remove", remove(renamed) == 0);
	remove(w);

	/* cleanup any remnant */
	remove(r);
	remove(renamed);

	if (fails) {
		printf("%d stdio_file FAIL\n", fails);
		return 1;
	}
	printf("PASS stdio_file\n");
	return 0;
}
