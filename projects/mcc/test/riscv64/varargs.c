typedef __builtin_va_list va_list;

long
rv64_varargs_register(int named, ...)
{
	va_list ap;

	__builtin_va_start(ap, named);
	return __builtin_va_arg(ap, long) + __builtin_va_arg(ap, long);
}

long
rv64_varargs_stack(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7, ...)
{
	va_list ap;

	__builtin_va_start(ap, a7);
	return __builtin_va_arg(ap, long) + __builtin_va_arg(ap, long);
}
