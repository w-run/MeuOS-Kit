/* wchar_stream_gate.c — wide-character stream I/O (fgetwc/fputwc/ungetwc/fwide).
 *
 * Exercises the wchar FILE stream functions.  The C locale is byte-oriented:
 * fputwc emits the wide char's low byte, fgetwc reads it back; fwide honours
 * the stream-orientation contract (undecided -> byte/wide on first lock).
 * Round-trips use tmpfile() for the write path; fmemopen() (read-only in
 * this libc) exercises the read path.  Wide values are built as integers so
 * the gate does not depend on wide-literal lowering. */
#include <wchar.h>
#include <stdio.h>

static int fails;

static void chk(const char *lbl, int cond)
{
	if (!cond) {
		printf("FAIL: %s\n", lbl);
		fails++;
	}
}

static FILE *newtmp(void)
{
	FILE *f = tmpfile();
	if (!f)
		printf("FAIL: tmpfile\n");
	return f;
}

int main(void)
{
	/* --- fputwc then fgetwc round trip (fd-backed stream) --- */
	{
		FILE *f = newtmp();
		if (!f) return 1;
		chk("fpidx undecided", fwide(f, 0) == 0);
		chk("fputwc A", fputwc((wchar_t)'A', f) == (wint_t)'A');
		chk("fputwc B", fputwc((wchar_t)'B', f) == (wint_t)'B');
		chk("fwide wide 0", fwide(f, 0) == 1);   /* first wide op locked wide */
		rewind(f);
		chk("fgetwc A", fgetwc(f) == (wint_t)'A');
		chk("fgetwc B", fgetwc(f) == (wint_t)'B');
		chk("fgetwc eof", fgetwc(f) == WEOF);
		fclose(f);
	}

	/* --- wide orientation locks via fwide itself --- */
	{
		FILE *f = newtmp();
		if (!f) return 1;
		chk("wide set", fwide(f, 1) == 1);
		chk("wide read", fwide(f, 0) == 1);
		fclose(f);
	}

	/* --- byte orientation locks via fwide --- */
	{
		FILE *f = newtmp();
		if (!f) return 1;
		chk("byte set", fwide(f, -1) == -1);
		chk("byte read", fwide(f, 0) == -1);
		fclose(f);
	}

	/* --- fgetwc reads a read-only memory stream --- */
	{
		char src[] = "XY";
		FILE *f = fmemopen(src, 2, "r");
		if (!f) { printf("FAIL: fmemopen\n"); return 1; }
		chk("fgetwc X", fgetwc(f) == (wint_t)'X');
		chk("fgetwc Y", fgetwc(f) == (wint_t)'Y');
		chk("fgetwc eof", fgetwc(f) == WEOF);
		fclose(f);
	}

	/* --- ungetwc pushes back one wide char (fd-backed) --- */
	{
		FILE *f = newtmp();
		if (!f) return 1;
		fputwc((wchar_t)'Q', f);
		rewind(f);
		wint_t c = fgetwc(f);
		chk("get Q", c == (wint_t)'Q');
		chk("unget Q", ungetwc(c, f) == (wint_t)'Q');
		chk("reget Q", fgetwc(f) == (wint_t)'Q');
		fclose(f);
	}

	if (fails) {
		printf("%d wchar_stream FAIL\n", fails);
		return 1;
	}
	printf("PASS wchar_stream\n");
	return 0;
}
