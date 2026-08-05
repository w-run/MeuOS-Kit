/* strtok_gate.c — strtok()/strtok_r() regression gate.
 *
 * Verifies tokenisation: delimiter splitting, the persistent state across
 * calls (strtok) vs. the explicit saveptr (strtok_r), and that both agree
 * on the same input. */
#include <string.h>
#include <stdio.h>

static int fails;

static void
chk(const char *lbl, const char *got, const char *want)
{
	if (got == NULL ? want != NULL : strcmp(got, want) != 0) {
		printf("FAIL: %s got=%s want=%s\n", lbl, got ? got : "(null)", want);
		fails++;
	}
}

int
main(void)
{
	/* classic strtok with persistent state.
	 * delimiter is ',' only, so the ';;' run is inside one token. */
	char s1[] = "one,two;;three";
	char *p;

	p = strtok(s1, ",");
	chk("tok1", p, "one");
	p = strtok(NULL, ",");
	chk("tok2", p, "two;;three");
	p = strtok(NULL, ",");
	chk("tok-end", p, NULL);

	/* strtok_r uses an explicit context, so two scans can run in parallel */
	{
		char a[] = "x,y,z";
		char b[] = "1 2 3";
		char *spa = 0, *spb = 0, *ta, *tb;

		ta = strtok_r(a, ",", &spa);
		tb = strtok_r(b, " ", &spb);
		chk("rtok a1", ta, "x");
		chk("rtok b1", tb, "1");

		/* advance a; b's context is untouched */
		ta = strtok_r(NULL, ",", &spa);
		chk("rtok a2", ta, "y");
		chk("rtok b2 still 1", tb, "1");

		tb = strtok_r(NULL, " ", &spb);
		chk("rtok b2", tb, "2");
		ta = strtok_r(NULL, ",", &spa);
		chk("rtok a3", ta, "z");
	}

	/* leading delimiters are skipped; a purely-delimited string yields NULL */
	{
		char s3[] = ",,,,";
		chk("empty strtok", strtok(s3, ","), NULL);
	}

	if (fails) {
		printf("%d strtok FAIL\n", fails);
		return 1;
	}
	printf("PASS strtok\n");
	return 0;
}
