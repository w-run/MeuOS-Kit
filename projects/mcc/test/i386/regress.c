int
sum6(int a, int b, int c, int d, int e, int f)
{
	return a + b + c + d + e + f;
}

long long
wide_add(long long a, long long b)
{
	return a + b;
}

int
entry(void)
{
	return sum6(1, 2, 3, 4, 5, 6) + (int)wide_add(7, 8);
}
