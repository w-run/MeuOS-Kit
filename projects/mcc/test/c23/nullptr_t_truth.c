/* C23 nullptr_t in truth contexts.
 *
 * Regression for F2: a nullptr_t value in a truth context (if / while /
 * ternary / unary !) previously crashed mcc with "internal error;
 * unsupported conversion".  A nullptr_t value is the null pointer
 * constant, i.e. always false.
 *
 * nullptr_t is a builtin type name (equivalent to <stddef.h>'s typedef);
 * no include is needed so the test also runs under --specs=host.
 */

int main(void) {
	nullptr_t np = nullptr;

	if (np) return 1;                  /* false */
	if (!np) { } else return 2;        /* !np is true */
	while (np) return 3;               /* body skipped */

	int t = np ? 1 : 0;                /* ternary: false arm */
	if (t != 0) return 4;

	if ((_Bool)np != 0) return 5;      /* explicit cast to bool */

	/* nullptr_t comparisons (== and != with pointers are still valid) */
	int x = 1;
	int *p = &x;
	if (p == nullptr) return 6;        /* 1 != null */
	if (nullptr != np) return 7;       /* null == null */

	return 0;
}
