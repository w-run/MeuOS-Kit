struct pair {
	double x;
	double y;
};

struct big {
	long a;
	long b;
	long c;
};

extern long sum8(long, long, long, long, long, long, long, long);
extern double addd(double, double);
extern struct pair pair_add(struct pair);
extern struct big big_id(struct big);

long
rv64_abi_probe(long x)
{
	struct pair p = {1.0, 2.0};
	struct big b = {x, x + 1, x + 2};

	return sum8(x, 1, 2, 3, 4, 5, 6, 7)
		+ (long)addd(p.x, p.y)
		+ (long)pair_add(p).x
		+ big_id(b).c;
}
