/* C23: nullptr_t is the type of the nullptr keyword and is usable as a
 * type name (provided as a builtin, equivalent to <stddef.h>'s typedef). */
int main(void) {
	nullptr_t p = nullptr;
	nullptr_t q;
	nullptr_t s;

	q = p;
	if (p != nullptr) return 1;
	if (q != nullptr) return 2;

	s = q;
	if (s != p) return 3;

	int *r = nullptr;
	if (r) return 4;
	if (r != nullptr) return 5;

	void *v = nullptr;
	if (v != nullptr) return 6;

	return 0;
}
