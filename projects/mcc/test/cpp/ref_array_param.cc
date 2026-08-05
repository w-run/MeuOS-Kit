// ref_array_param.cc -- C++11 reference-to-array parameters int(&a)[3]
//
// A reference-to-array parameter binds the array object itself (no
// decay to pointer); element access must work through the reference.
int sum(int(&a)[3]) { return a[0] + a[1] + a[2]; }

// mutation through the reference must reach the caller's array
int bump(int(&b)[3]) { b[1] += 100; return b[1]; }

// function-reference parameter: int(&f)(int, int) binds a function
int apply(int(&f)(int, int), int x, int y) { return f(x, y); }
int add2(int x, int y) { return x + y; }

// multi-dim array reference
int rowsum(int(&m)[2][2])
{
	int t = 0, i, j;
	for (i = 0; i < 2; ++i)
		for (j = 0; j < 2; ++j)
			t += m[i][j];
	return t;
}

int main(void)
{
	int v[3] = { 10, 20, 30 };
	if (sum(v) != 60)
		return 1;
	if (bump(v) != 120)
		return 2;
	if (v[1] != 120)
		return 3;
	if (apply(add2, 3, 4) != 7)
		return 4;
	int m[2][2] = { { 1, 2 }, { 3, 4 } };
	if (rowsum(m) != 10)
		return 5;
	return 0;
}
