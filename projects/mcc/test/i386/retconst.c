/* i386 constant-return regression test (compile-time gate in regress.sh).
 *
 * Guards the i64 return-lowering fix in i386_mabi.c (mabi_selret): a bare
 * `ret (i64)const` for a non-negative int constant must emit an immediate
 * `movl $imm, %eax` (plus the high half into EDX), NOT a load from
 * `[ebp + s0->slot]`.  Constants have no stack slot, so the old code read
 * garbage (e.g. `movl -1(%ebp), %eax; movl 3(%ebp), %eax`) and returned
 * random values (observed as 0 instead of 42).
 *
 * The functions deliberately use plain constant returns with *no* `static`
 * keyword so they are emitted as machine functions (not fully folded away),
 * keeping the codegen path exercised.
 */
int ret42(void) { return 42; }
int ret0(void)  { return 0; }
int ret1000(void){ return 1000; }
int retneg(void){ return -1; }
long long ret42ll(void){ return 42LL; }
long long retnegll(void){ return -1LL; }
