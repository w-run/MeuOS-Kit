/* C23: all four standard attributes parse and are accepted. */
[[deprecated]] int old_func(void) { return 1; }
[[maybe_unused]] static int unused_var = 42;
[[nodiscard]] int produce(void) { return 7; }
[[nodiscard]] int make(void);
int make(void) { return 8; }

int main(void) {
	int x = 0;

	(void)old_func();
	switch (x) {
	case 0:
		x = produce();
		[[fallthrough]];
	case 1:
		x += make();
		break;
	}
	(void)unused_var;
	return x > 0 ? 0 : 1;
}
