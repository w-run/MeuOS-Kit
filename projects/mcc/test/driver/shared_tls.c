/* Local-exec TLS is usable in every currently non-i386 backend. */
static _Thread_local int shared_tls = 11;
static _Thread_local int shared_tls_mutable = 4;

int
shared_float_order(double left, double right)
{
	return left < right && right >= left && left != right;
}

int
shared_tls_addressable(void)
{
	int *address = &shared_tls;
	return *address;
}

int
shared_tls_write(void)
{
	int *address = &shared_tls_mutable;
	*address += 3;
	return *address;
}
