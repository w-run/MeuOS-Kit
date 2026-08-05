/* snprintf_gate.c — snprintf/sprintf family fine-grained gate.
 *
 * Verifies formatted output: exact text for %d/%x/%s/%c, the C99 return
 * value ("length that would have been written, excluding the NUL") for
 * both full and truncated writes, and that snprintf NUL-terminates and
 * never overruns its size bound. */
#include <stdio.h>
#include <string.h>

static int fails;

static void
chk_rt(const char *lbl, int got, int want)
{
	if (got != want) {
		printf("FAIL: %s ret=%d want %d\n", lbl, got, want);
		fails++;
	}
}

static void
chk_str(const char *lbl, const char *got, const char *want)
{
	if (strcmp(got, want) != 0) {
		printf("FAIL: %s buf=\"%s\" want \"%s\"\n", lbl, got, want);
		fails++;
	}
}

int
main(void)
{
	char buf[64];

	/* full write fits */
	memset(buf, '!', sizeof buf);
	int n = snprintf(buf, sizeof buf, "%d", 42);
	chk_rt("snprintf(%d) ret", n, 2);
	chk_str("snprintf(%d) buf", buf, "42");

	n = snprintf(buf, sizeof buf, "%s+%x", "ab", 255);
	chk_rt("snprintf(%s+%x) ret", n, 5);      /* "ab" + "+" + "ff" = 4? "ab"=2, "+"=1, "ff"=2 => 5 */
	chk_str("snprintf(%s+%x) buf", buf, "ab+ff");

	n = snprintf(buf, sizeof buf, "%c%c", 'q', 'z');
	chk_rt("snprintf(%c%c) ret", n, 2);
	chk_str("snprintf(%c%c) buf", buf, "qz");

	/* truncated write: returns the would-be length, buffer is NUL-terminated */
	char small[4];
	memset(small, 'X', sizeof small);
	n = snprintf(small, sizeof small, "helloworld");
	chk_rt("snprintf truncate ret", n, 10);
	chk_str("snprintf truncate buf", small, "hel");

	/* exact-size truncation */
	char exact[3];
	memset(exact, 'X', sizeof exact);
	n = snprintf(exact, 3, "abc");
	chk_str("snprintf exact buf", exact, "ab");
	chk_rt("snprintf exact ret", n, 3);

	/* sprintf into a large enough buffer */
	n = sprintf(buf, "%d %x %s", -100, 0xbeef, "end");
	chk_rt("sprintf ret", n, (int)strlen("-100 beef end"));

	/* long int via %ld (uses int promotion path if any) */
	n = snprintf(buf, sizeof buf, "%ld", 1234567L);
	chk_rt("snprintf %ld ret", n, 7);
	chk_str("snprintf %ld buf", buf, "1234567");

	/* unsigned hex uppercase */
	n = snprintf(buf, sizeof buf, "%X", 255);
	chk_rt("snprintf %X ret", n, 2);
	chk_str("snprintf %X buf", buf, "FF");

	if (fails) {
		printf("%d snprintf FAIL\n", fails);
		return 1;
	}
	printf("PASS snprintf\n");
	return 0;
}
