/* Test inline asm and _Pragma (no-op) */
int main(void) {
	__asm__("nop");
	__asm__ volatile("nop");
	__asm__("nop" : : : "memory");
	return 0;
}
