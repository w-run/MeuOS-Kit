/* loongarch64 PIE (position-independent executable) test.
 *
 * Verifies that mcc emits PC-relative addressing for global variables
 * and function calls when -fPIE is used.  The assembly is checked by
 * regress.sh for correct pcalau12i + %pc_hi20 / %pc_lo12 sequences.
 */
int global_var = 42;

int get_global(void) { return global_var; }

int main(void) { return get_global() - 42; }