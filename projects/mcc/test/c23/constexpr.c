/* Test C23 constexpr */
int main(void) {
	constexpr int N = 42;
	return N == 42 ? 0 : 1;
}
