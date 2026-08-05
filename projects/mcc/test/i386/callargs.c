/* i386 call-argument stack placement regression.
 *
 * The outgoing stack-argument block is reserved to a 16-byte boundary, and
 * the arguments must occupy the *bottom* of that block so they land at
 * [ebp+8].. once the callee's `call`+prologue push the return address and
 * frame pointer.  mabi_selcall previously started the write cursor at the
 * 16-aligned reservation size, so a 2-int-arg call (8 bytes, padded to 16)
 * wrote the args at [esp+8]/[esp+12] instead of [esp+0]/[esp+4]; the callee
 * then read uninitialized stack at [ebp+8]/[ebp+12] -> random result
 * (rr_call in the cross-arch matrix returned 0 instead of 42).
 *
 * This compiles to assembly; test/i386/regress.sh asserts the two args are
 * stored at (%esp) and 4(%esp), not (esp+8)/(esp+12).
 */
static int add(int a, int b) { return a + b; }

int main(void) { return add(20, 22); }
