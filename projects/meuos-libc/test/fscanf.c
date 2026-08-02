/* fscanf / vfscanf / vscanf: C99 7.19.6.2 stream input conversions.
 *
 * Uses fmemopen-backed memory streams so the test is self-contained.
 * (Kept out of test/stdio.c: that program's local-variable layout trips a
 * mcc codegen defect when a fscanf-with-va_list block follows another
 * fmemopen block; the standalone binary below is stable.)
 */
#include <stdio.h>
#include <string.h>

int
main(void)
{
	char buf[32] = "77 5f tail";
	FILE *stream;
	int decimal = 0, hexadecimal = 0;
	char word[16];

	stream = fmemopen(buf, sizeof buf, "r");
	if (!stream)
		return 1;
	if (fscanf(stream, "%d %x", &decimal, &hexadecimal) != 2
	 || decimal != 77 || hexadecimal != 0x5f)
		return 2;
	if (fclose(stream) != 0)
		return 3;

	/* trailing text remains after the two conversions */
	strcpy(buf, "word 42");
	stream = fmemopen(buf, sizeof buf, "r");
	if (!stream)
		return 4;
	if (fscanf(stream, "%s %d", word, &decimal) != 2
	 || strcmp(word, "word") != 0 || decimal != 42)
		return 5;
	if (fclose(stream) != 0)
		return 6;

	/* floating-point conversions: %f -> float, %lf -> double */
	{
		float f;
		double d;
		strcpy(buf, "3.5 1.25e2");
		stream = fmemopen(buf, sizeof buf, "r");
		if (!stream)
			return 10;
		if (fscanf(stream, "%f %lf", &f, &d) != 2
		 || f != 3.5f || d != 125.0)
			return 11;
		if (fclose(stream) != 0)
			return 12;
	}

	/* base-detecting and unsigned integer conversions */
	{
		int iv;
		unsigned u1, u2;
		strcpy(buf, "0x1f 010 42");
		stream = fmemopen(buf, sizeof buf, "r");
		if (!stream)
			return 13;
		if (fscanf(stream, "%i %o %u", &iv, &u1, &u2) != 3
		 || iv != 31 || u1 != 8u || u2 != 42u)
			return 14;
		if (fclose(stream) != 0)
			return 15;
	}

	/* EOF on an empty stream (/dev/null reads EOF immediately) */
	stream = fopen("/dev/null", "r");
	if (!stream)
		return 7;
	if (fscanf(stream, "%d", &decimal) != EOF)
		return 8;
	if (fclose(stream) != 0)
		return 9;

	puts("PASS fscanf");
	return 0;
}
