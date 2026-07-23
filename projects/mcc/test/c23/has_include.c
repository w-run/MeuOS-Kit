/* Test C23 __has_include */
int main(void) {
#if __has_include("embed.dat")
	int result = 42;
#else
	int result = 0;
#endif
	return result != 42;
}
