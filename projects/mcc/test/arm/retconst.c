/* arm constant-return regression test (compile-time gate in regress.sh).
 *
 * Guards the i64 return-lowering fix in arm_mabi.c (mabi_selret): a bare
 * `ret (i64)const` for a non-negative int constant must emit an immediate
 * move (movw/mvn), NOT a load from [fp + s0->slot].  Constants have no
 * stack slot, so the old code read garbage off the uninitialized frame
 * (e.g. `add r12,r12,#-1; ldr r10,[r12]` for `return 42`).
 *
 * Functions are non-static so they are emitted (not constant-folded away).
 */
int ret42(void) { return 42; }
int ret0(void)  { return 0; }
int ret1000(void){ return 1000; }
int retneg(void){ return -1; }
long long ret42ll(void){ return 42LL; }
