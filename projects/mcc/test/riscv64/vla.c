int
rv64_vla(int n)
{
	int v[n];

	v[0] = 1;
	return v[0];
}

int
rv64_large_vla(int n)
{
	char pad[4096];
	int v[n];

	v[0] = 1;
	return pad[0] + v[0];
}
