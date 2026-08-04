/* uniform_init.cc — C++11 uniform initialization `T x{...}`.
 *
 * Brace-init of an aggregate object (and of arrays) with a braced
 * element list.  Returns 0 on success.
 */
struct P {
	int x;
	int y;
};

int
main(void)
{
	P p{3, 4};
	if (p.x != 3) return 1;
	if (p.y != 4) return 2;

	int a[]{1, 2, 3};
	if (a[2] != 3) return 3;

	return 0;
}
