// i386 TLS (Thread-Local Storage) end-to-end regression test.
// Verifies TLS model selection, relocation, and runtime access.

static _Thread_local int tls_counter;
static _Thread_local int tls_initialized = 42;

int
i386_tls_increment(void)
{
	return ++tls_counter;
}

int
main(void)
{
	// Test TLS read (initialized value)
	if (tls_initialized != 42)
		return 1;

	// Test TLS write and read-back
	tls_initialized = 100;
	if (tls_initialized != 100)
		return 2;

	// Test TLS increment via separate function
	if (i386_tls_increment() != 1)
		return 3;
	if (i386_tls_increment() != 2)
		return 4;

	return 0;
}
