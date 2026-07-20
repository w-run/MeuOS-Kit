#include <stdio.h>
#include <stdlib.h>

int
main(void)
{
	int *values = malloc(2 * sizeof(*values));
	int *grown;
	char *zeroed = calloc(8, 1);

	if (!values || !zeroed)
		return 1;
	values[0] = 17;
	values[1] = 25;
	grown = realloc(values, 4 * sizeof(*values));
	if (!grown || grown[0] != 17 || grown[1] != 25)
		return 1;
	if (zeroed[0] || zeroed[7])
		return 1;
	free(grown);
	free(zeroed);
	puts("PASS minimal malloc");
	return 0;
}
