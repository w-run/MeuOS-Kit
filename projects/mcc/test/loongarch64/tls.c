static _Thread_local int tls_counter;

int
la64_tls_increment(void)
{
	return ++tls_counter;
}
