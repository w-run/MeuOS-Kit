/* getopt_gate.c — getopt() regression gate.
 * Exercises required-arg, optional-arg, grouped options, unknown option,
 * "--" end marker, and the optind state after parsing. */
#include <unistd.h>
#include <stdio.h>
#include <string.h>

static int fails;

int
main(int argc, char *argv[])
{
	(void)argc;

	/* Case A: -a, -b needs an arg (next element), -- marker, then operands.
	 * argv (rebuilt): prog -a -b val -- rest1 rest2 */
	{
		char *a[] = { (char *)"prog", (char *)"-a", (char *)"-b",
		              (char *)"val", (char *)"--", (char *)"rest1",
		              (char *)"rest2", NULL };
		int c;
		int oa = 0, ob = 0; char *barg = NULL;
		optind = 1;
		while ((c = getopt(7, a, "ab:")) != -1) {
			if (c == 'a') oa = 1;
			else if (c == 'b') { ob = 1; barg = optarg; }
		}
		if (!oa) { printf("FAIL A: -a not parsed\n"); fails++; }
		if (!ob || !barg || strcmp(barg, "val") != 0) {
			printf("FAIL A: -b arg='%s'\n", barg ? barg : "NULL"); fails++;
		}
		if (optind != 5) { printf("FAIL A: optind=%d want 5 (rest1)\n", optind); fails++; }
		if (strcmp(a[optind], "rest1") != 0 || strcmp(a[optind+1], "rest2") != 0) {
			printf("FAIL A: operands after optind\n"); fails++;
		}
	}

	/* Case B: grouped -abval where b takes the argument from the same token */
	{
		char *b[] = { (char *)"prog", (char *)"-abval", NULL };
		int c; char *barg = NULL; int oa = 0, ob = 0;
		optind = 1;
		while ((c = getopt(2, b, "ab:")) != -1) {
			if (c == 'a') oa = 1;
			else if (c == 'b') { ob = 1; barg = optarg; }
		}
		if (!oa || !ob || !barg || strcmp(barg, "val") != 0) {
			printf("FAIL B: grouped abval oa=%d ob=%d barg=%s\n", oa, ob, barg?barg:"NULL");
			fails++;
		}
		if (optind != 2) { printf("FAIL B: optind=%d want 2\n", optind); fails++; }
	}

	/* Case C: optional arg — "-c out": argument NOT taken from next element;
	 * "-cout": argument taken from same token. */
	{
		char *c1[] = { (char *)"prog", (char *)"-c", (char *)"out", NULL };
		char *c2[] = { (char *)"prog", (char *)"-cout", NULL };
		int cc; char *carg = NULL;
		optind = 1;
		while ((cc = getopt(3, c1, "ab:c::")) != -1) {
			if (cc == 'c') carg = optarg;
		}
		if (carg != NULL) { printf("FAIL C: -c out should have no arg, got '%s'\n", carg); fails++; }
		if (optind != 2) { printf("FAIL C: -c out optind=%d want 2\n", optind); fails++; }
		optind = 1; carg = NULL;
		while ((cc = getopt(2, c2, "ab:c::")) != -1) {
			if (cc == 'c') carg = optarg;
		}
		if (!carg || strcmp(carg, "out") != 0) {
			printf("FAIL C: -cout should have arg 'out', got '%s'\n", carg?carg:"NULL");
			fails++;
		}
	}

	/* Case D: unknown option -> '?' */
	{
		char *d[] = { (char *)"prog", (char *)"-z", NULL };
		int cc; int saw_q = 0;
		optind = 1;
		while ((cc = getopt(2, d, "ab:")) != -1)
			if (cc == '?') saw_q = 1;
		if (!saw_q) { printf("FAIL D: unknown -z should return '?'\n"); fails++; }
	}

	/* Case E: missing required arg -> '?' (colons: suppresses printing) */
	{
		char *e[] = { (char *)"prog", (char *)"-b", NULL };
		int cc; int saw_q = 0;
		optind = 1;
		while ((cc = getopt(2, e, "ab:")) != -1)
			if (cc == '?') saw_q = 1;
		if (!saw_q) { printf("FAIL E: missing -b arg should return '?'\n"); fails++; }
	}

	/* Case F: a plain operand stops the scan (lone '-' too) */
	{
		char *f[] = { (char *)"prog", (char *)"plainfile", NULL };
		optind = 1;
		int cc = getopt(2, f, "ab:");
		if (cc != -1) { printf("FAIL F: operand should end scan, got %c\n", cc); fails++; }
		if (optind != 1) { printf("FAIL F: optind=%d want 1\n", optind); fails++; }
	}
	/* Case F2: a lone '-' is also an operand (does not start option) */
	{
		char *f2[] = { (char *)"prog", (char *)"-", NULL };
		optind = 1;
		int cc = getopt(2, f2, "ab:");
		if (cc != -1) { printf("FAIL F2: lone '-' should end scan, got %c\n", cc); fails++; }
		if (optind != 1) { printf("FAIL F2: optind=%d want 1\n", optind); fails++; }
	}

	if (fails) {
		printf("%d getopt FAIL\n", fails);
		return 1;
	}
	printf("PASS getopt\n");
	return 0;
}
