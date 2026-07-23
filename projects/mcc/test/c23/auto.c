/* Test C23 auto type deduction */
int main(void) {
	auto x = 42;
	return x == 42 ? 0 : 1;
}
