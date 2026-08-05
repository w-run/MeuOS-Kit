/* NEGATIVE C23 test: a constexpr function whose body is not a constant
 * expression (it performs I/O) must be rejected at translation time.
 *
 * Expected: mcc error (constexpr function body not constant-foldable).
 * Verified by run-neg.sh expecting a non-zero compile exit.
 */
extern int printf(const char *, ...);

constexpr int bad(void) {
	printf("not constant\n");
	return 1;
}

int main(void) { return 0; }
