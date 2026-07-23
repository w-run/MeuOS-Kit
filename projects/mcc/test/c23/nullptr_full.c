/* Test C23 nullptr_t full semantics */
int main(void) {
	int *p = nullptr;
	if (p != nullptr) return 1;
	return 0;
}
