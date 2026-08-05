#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <obstack.h>
#include <argp.h>
#include <string2.h>
#include <libgen.h>
#include <program-invocation.h>

/* --- getline / getdelim --- */
static int
test_getline(void)
{
	FILE *f = fmemopen((char *)"hello world\nline two\n", 21, "r");
	char *line = NULL;
	size_t cap = 0;
	ssize_t len;

	if (!f)
		return 1;
	len = getline(&line, &cap, f);
	if (len != 12 || strcmp(line, "hello world\n") != 0)
		return 2;
	len = getline(&line, &cap, f);
	if (len != 9 || strcmp(line, "line two\n") != 0)
		return 3;
	len = getline(&line, &cap, f);
	if (len != -1)
		return 4;
	fclose(f);
	free(line);
	return 0;
}

/* --- asprintf --- */
static int
test_asprintf(void)
{
	char *s = NULL;
	int len = asprintf(&s, "x=%d y=%s", 42, "hi");

	if (len != 9 || strcmp(s, "x=42 y=hi") != 0)
		return 1;
	free(s);
	return 0;
}

/* --- obstack --- */
static int
test_obstack(void)
{
	struct obstack obs;
	char *p;
	int i;

	obstack_init(&obs, 128);
	for (i = 0; i < 3; ++i)
		obstack_1grow(&obs, 'A' + i);
	obstack_1grow(&obs, '\0');
	p = obstack_finish(&obs);
	if (strcmp(p, "ABC") != 0)
		return 1;
	obstack_grow(&obs, "hello", 5);
	obstack_grow0(&obs, " world", 6);
	p = obstack_finish(&obs);
	if (strcmp(p, "hello world") != 0)
		return 2;
	obstack_free(&obs, NULL);
	return 0;
}

/* --- error (no exit) --- */
static int
test_error(void)
{
	/* Call with status=0 so it prints but does not exit. */
	error(0, 0, "test error message (expected)");
	error_at_line(0, 0, __FILE__, __LINE__, "test at_line (expected)");
	if (error_message_count < 2)
		return 1;
	return 0;
}

/* --- funopen / fopencookie --- */
static int cookie_read_calls;
static int cookie_write_calls;
static int cookie_pos;
static int cookie_close_calls;

static int
my_read(void *cookie, char *buf, int len)
{
	const char *src = cookie;
	int avail = (int)strlen(src) - cookie_pos;

	if (avail <= 0)
		return 0;
	if (avail > len)
		avail = len;
	memcpy(buf, src + cookie_pos, avail);
	cookie_pos += avail;
	return avail;
}

static int
my_write(void *cookie, const char *buf, int len)
{
	(void)cookie;
	cookie_write_calls += len;
	return len;
}

static int
my_close(void *cookie)
{
	(void)cookie;
	cookie_close_calls = 1;
	return 0;
}

static int
test_funopen(void)
{
	FILE *f = funopen("cookie-data", my_read, NULL, NULL, my_close);
	char buf[32];

	if (!f)
		return 1;
	if (fgets(buf, sizeof(buf), f) == NULL)
		return 2;
	if (strcmp(buf, "cookie-data") != 0)
		return 3;
	fclose(f);
	if (!cookie_close_calls)
		return 4;

	/* fopencookie write test */
	cookie_write_calls = 0;
	cookie_close_calls = 0;
	f = fopencookie(NULL, "w",
	    (cookie_io_functions_t){NULL, (void *)my_write, NULL, my_close});
	if (!f)
		return 5;
	fputs("hello", f);
	fclose(f);
	if (cookie_write_calls != 5)
		return 6;
	return 0;
}

/* --- argp --- */
static int saw_foo;
static int saw_bar;
static char *bar_arg;

static error_t
my_parser(int key, char *arg, struct argp_state *state)
{
	(void)state;
	switch (key) {
	case 'f':
		saw_foo = 1;
		return 0;
	case 'b':
		saw_bar = 1;
		bar_arg = arg;
		return 0;
	case 'A':
		return 0;
	case 0:
		return 0;
	default:
		return ARGP_ERR_UNKNOWN;
	}
}

static int
test_argp(void)
{
	const struct argp_option opts[] = {
		{"foo", 'f', 0, 0, "foo flag"},
		{"bar", 'b', "VAL", 0, "bar option"},
		{0, 0, 0, 0, 0}
	};
	const struct argp my_argp = {opts, my_parser, "ARGS", "test argp"};
	char *argv[] = {"prog", "--foo", "--bar=xyz", "pos1", NULL};
	error_t e;

	saw_foo = 0;
	saw_bar = 0;
	bar_arg = NULL;
	e = argp_parse(&my_argp, 4, argv, 0, NULL, NULL);
	if (e != 0)
		return 1;
	if (!saw_foo || !saw_bar || strcmp(bar_arg, "xyz") != 0)
		return 2;
	return 0;
}

/* --- program_invocation (glibc convention globals) --- */
static int
test_program_invocation(int argc, char **argv)
{
	char *shortname;

	/* Core startup must have populated both from argv[0]. */
	if (argc < 1 || !argv[0])
		return 1;
	if (!program_invocation_name) {
		puts("FAIL program_invocation_name is NULL");
		return 2;
	}
	if (program_invocation_name != argv[0]) {
		printf("FAIL program_invocation_name=%s argv[0]=%s\n",
		       program_invocation_name, argv[0]);
		return 3;
	}
	if (!program_invocation_short_name) {
		puts("FAIL program_invocation_short_name is NULL");
		return 4;
	}
	/* short_name must equal the basename of argv[0]. */
	shortname = basename(argv[0]);
	if (strcmp(program_invocation_short_name, shortname) != 0) {
		printf("FAIL short_name=%s basename=%s\n",
		       program_invocation_short_name, shortname);
		return 5;
	}
	return 0;
}

int
main(int argc, char **argv)
{
	int r;

	if ((r = test_getline()))
		printf("FAIL getline: %d\n", r);
	else if ((r = test_asprintf()))
		printf("FAIL asprintf: %d\n", r);
	else if ((r = test_obstack()))
		printf("FAIL obstack: %d\n", r);
	else if ((r = test_error()))
		printf("FAIL error: %d\n", r);
	else if ((r = test_funopen()))
		printf("FAIL funopen: %d\n", r);
	else if ((r = test_argp()))
		printf("FAIL argp: %d\n", r);
	else if ((r = test_program_invocation(argc, argv)))
		printf("FAIL program_invocation: %d\n", r);
	else
		printf("compat ok\n");
	return r;
}
