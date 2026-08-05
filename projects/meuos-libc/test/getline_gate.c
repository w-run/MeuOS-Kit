/* getline/getdelim regression gate (now core libc).
 * Covers: empty line, long line (buffer growth), EOF, multi-line,
 * custom delimiter. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(void)
{
	FILE *f;
	char *line = NULL;
	size_t cap = 0;
	ssize_t n;
	int count = 0;

	/* Materialize a test stream in-memory via tmpfile + write. */
	f = tmpfile();
	if (!f)
		return 1;

	/* empty line, short line, and a long line exceeding the 128 starter cap */
	fputs("\nhello\n", f);
	{
		char longbuf[512];
		memset(longbuf, 'x', sizeof longbuf);
		longbuf[510] = '\n';
		longbuf[511] = '\0';
		fputs(longbuf, f);
	}
	fputs("alpha", f);       /* no trailing newline: EOF-term line */
	if (fseek(f, 0, SEEK_SET) != 0)
		return 2;

	/* line 1: empty line ("\n") -> 1 byte, the newline itself */
	n = getline(&line, &cap, f);
	if (n != 1 || line[0] != '\n')
		return 10;
	count++;
	/* line 2: "hello" (5 chars + '\n' = 6) */
	n = getline(&line, &cap, f);
	if (n != 6 || strncmp(line, "hello", 5) != 0)
		return 11;
	count++;
	/* line 3: long line (510 x's + '\n') -> must have grown the buffer */
	n = getline(&line, &cap, f);
	if (n != 511 || line[510] != '\n' || (size_t)n > cap)
		return 12;
	count++;
	/* line 4: "alpha" EOF-terminated (no newline) */
	n = getline(&line, &cap, f);
	if (n != 5 || strncmp(line, "alpha", 5) != 0)
		return 13;
	count++;
	/* EOF next -> -1 */
	n = getline(&line, &cap, f);
	if (n != -1)
		return 14;

	/* custom delimiter via getdelim on a fresh stream */
	{
		FILE *f2 = tmpfile();
		char *l2 = NULL;
		size_t c2 = 0;

		if (!f2)
			return 20;
		fputs("a:b:c", f2);
		rewind(f2);
		/* getdelim includes the delimiter: "a:" len 2, "b:" len 2, "c" len 1 */
		if (getdelim(&l2, &c2, ':', f2) != 2 || strcmp(l2, "a:") != 0)
			return 21;
		if (getdelim(&l2, &c2, ':', f2) != 2 || strcmp(l2, "b:") != 0)
			return 22;
		if (getdelim(&l2, &c2, ':', f2) != 1 || strcmp(l2, "c") != 0)
			return 23;
		free(l2);
		fclose(f2);
	}

	free(line);
	fclose(f);
	printf("PASS getline (lines=%d)\n", count);
	return 0;
}
