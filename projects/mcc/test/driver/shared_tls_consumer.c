extern int shared_float_order(double, double);
extern int shared_tls_addressable(void);
extern int shared_tls_write(void);

int
main(void)
{
	return shared_tls_addressable() != 11 || shared_tls_write() != 7
		|| !shared_float_order(1.0, 2.0);
}
