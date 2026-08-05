/* stdio_stream_gate.c — file stream low-level & positioning gate.
 *
 * Verifies the posix stream interfaces beyond plain read/write:
 *  - position control: fseek(SEEK_SET/_CUR/_END) with exact ftell,
 *    and the fgetpos/fsetpos save+restore round trip
 *  - buffering modes: setvbuf accepts _IOFBF/_IOLBF/_IONBF, rejects invalid
 *  - error/EOF indicators: a failed write sets the error indicator
 *    (ferror), and clearerr clears it; reading past EOF sets feof
 * Complements stdio_file_gate (lifecycle) with low-level semantics. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

static int fails;

static void
chk(const char *lbl, int cond)
{
	if (!cond) {
		printf("FAIL: %s (errno=%d %s)\n", lbl, errno, strerror(errno));
		fails++;
	}
}

int
main(void)
{
	const char *path = "/tmp/meuos-stdio-stream-gate.tmp";
	const char *data = "0123456789ABCDEF";   /* 16 bytes */
	int fd;
	FILE *fp;

	/* build a 16-byte file */
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) { perror("open"); return 1; }
	write(fd, data, 16);
	close(fd);

	fp = fopen(path, "r+");
	if (!fp) { perror("fopen"); return 1; }

	/* setvbuf: valid modes accepted, invalid rejected w/ EOF+EINVAL */
	errno = 0;
	chk("setvbuf _IOFBF", setvbuf(fp, NULL, _IOFBF, 4096) == 0);
	chk("setvbuf _IOLBF", setvbuf(fp, NULL, _IOLBF, 0) == 0);
	chk("setvbuf _IONBF", setvbuf(fp, NULL, _IONBF, 0) == 0);
	errno = 0;
	chk("setvbuf invalid EOF", setvbuf(fp, NULL, 99, 0) == EOF);
	chk("setvbuf invalid EINVAL", errno == EINVAL);

	/* fseek SEEK_SET + ftell */
	errno = 0;
	chk("fseek SET", fseek(fp, 4, SEEK_SET) == 0);
	chk("ftell SET", ftell(fp) == 4);
	chk("getc @4", fgetc(fp) == '4');

	/* fseek SEEK_CUR back 2 from current (now 5) */
	chk("fseek CUR-2", fseek(fp, -2, SEEK_CUR) == 0);
	chk("ftell CUR", ftell(fp) == 3);
	chk("getc @3", fgetc(fp) == '3');

	/* fseek SEEK_END */
	chk("fseek END-3", fseek(fp, -3, SEEK_END) == 0);
	chk("ftell END", ftell(fp) == 13);
	chk("getc @13", fgetc(fp) == 'D');

	/* fgetpos save, move, fsetpos restore */
	{
		fpos_t save;
		chk("fgetpos", fgetpos(fp, &save) == 0);
		long saved = (long)save;               /* fpos_t is offset */
		fseek(fp, 0, SEEK_SET);
		chk("moved to 0", ftell(fp) == 0);
		chk("fsetpos restore", fsetpos(fp, &save) == 0);
		chk("ftell restored", (long)ftell(fp) == saved);
		chk("getc restored", fgetc(fp) == 'E'); /* 14th char 'E' */
	}

	/* error indicator: writing to a read-only stream */
	{
		FILE *ro = fopen(path, "r");          /* read-only handle */
		chk("ro fopen", ro != NULL);
		ferror(ro);                            /* seed; no-op */
		if (fputs("x", ro) == EOF) {
			chk("ferror set on ro write", ferror(ro) != 0);
			clearerr(ro);
			chk("clearerr cleared", ferror(ro) == 0);
		}
		fclose(ro);
	}

	/* EOF: reading past end sets feof (deterministic on memory streams) */
	{
		char mem[] = "ab";
		FILE *ms = fmemopen(mem, sizeof mem - 1, "r");
		chk("fmemopen", ms != NULL);
		char c;
		while (fread(&c, 1, 1, ms) > 0) {}
		chk("feof past end", feof(ms));
		clearerr(ms);
		fclose(ms);
	}

	fclose(fp);
	unlink(path);

	if (fails) {
		printf("%d stdio_stream FAIL\n", fails);
		return 1;
	}
	printf("PASS stdio_stream\n");
	return 0;
}
