/* C23 nullptr comparisons and contexts (7.21 / 6.4.4.6):
 *   - equality/inequality against nullptr
 *   - ternary selecting on a null pointer
 *   - nullptr passed as a function argument of pointer type
 *   - two null pointers compare equal
 *
 * NOTE: a `nullptr_t`-typed variable used directly in a truthiness
 * condition (if (n)) is excluded here — see the defect report (mcc
 * hits an internal error on that conversion).
 */
int f(void *p) { return p == nullptr ? 10 : 20; }

int main(void) {
	int *p = nullptr;
	if (p != nullptr) return 1;
	if (p == nullptr ? 0 : 1) return 2;

	int x = (p ? 100 : 200);
	if (x != 200) return 3;

	if (f(nullptr) != 10) return 4;
	if (f((void *)1) != 20) return 5;

	void *q = nullptr;
	if (!(p == q)) return 6;

	return 0;
}
