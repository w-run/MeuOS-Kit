#include <stddef.h>
#include <stdlib.h>

static void
swap(unsigned char *left, unsigned char *right, size_t width)
{
	while (width) {
		unsigned char temporary = *left;
		*left++ = *right;
		*right++ = temporary;
		--width;
	}
}

void
qsort(void *base, size_t count, size_t width, int (*compare)(const void *, const void *))
{
	unsigned char *items = base;
	size_t index;

	if (!items || !compare || width == 0)
		return;
	/* Insertion sort is intentionally compact and robust for bootstrap-scale lists. */
	for (index = 1; index < count; ++index) {
		size_t cursor = index;
		while (cursor && compare(items + (cursor - 1) * width, items + cursor * width) > 0) {
			swap(items + (cursor - 1) * width, items + cursor * width, width);
			--cursor;
		}
	}
}

void *
bsearch(const void *key, const void *base, size_t count, size_t width, int (*compare)(const void *, const void *))
{
	const unsigned char *items = base;

	while (count) {
		size_t middle = count / 2;
		const void *candidate = items + middle * width;
		int order = compare(key, candidate);
		if (order == 0)
			return (void *)candidate;
		if (order < 0)
			count = middle;
		else {
			items += (middle + 1) * width;
			count -= middle + 1;
		}
	}
	return 0;
}
