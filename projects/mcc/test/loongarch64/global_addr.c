/* loongarch64 global-address reloc gate.
 *
 * The global/function address sequence must use `pcalau12i %pc_hi20(sym)`
 * + `addi.d ... %pc_lo12(sym)`.  mt/ld's R_LARCH_PCALA_HI20 (71) is a
 * *page-relative* value ((S+A)>>12 - (P>>12)) which pairs with pcalau12i's
 * page-masked PC base ((PC & ~0xfff) + imm20<<12) and an *absolute* low 12
 * (case 72) to reconstruct the true address.  The pre-fix `pcaddu12i`
 * (full PC, not page-masked) yields a wrong address -> rr_global returned 0
 * and rr_call looped (deadlock 124).  Assert the emitted mnemonic.
 */
int g = 42;

int getg(void) { return g; }          /* global variable address */

int add2(int a, int b) { return a + b; }
int call_add(void) { return add2(10, 20); }  /* global function address */
