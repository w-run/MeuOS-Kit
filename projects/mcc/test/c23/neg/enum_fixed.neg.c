/* NEGATIVE C23 test: an enumerator value that does not fit the explicit
 * underlying type must be rejected (6.7.2.2).  `unsigned char` holds 0..255,
 * so 300 is out of range.
 *
 * Expected: mcc error. Verified by run-neg.sh expecting non-zero compile exit.
 */
enum Byte : unsigned char {
	A = 300
};

int main(void) { return 0; }
