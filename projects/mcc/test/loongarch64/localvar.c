/* loongarch64 function-entry CFG gate.
 *
 * A function that uses local variables (agglomerates/arrays are accessed
 * through a stack pointer set in the MIR *start* block) must branch from the
 * entry to that start block (`.L<fn>.bb0`).  The pre-fix emitter fell
 * straight through the prologue into whichever body block sat first in
 * emission order (`.bb1`), which dereferenced the uninitialised call-slot
 * `a0` for the local base → wrong memory / segfault (rr_struct on the
 * cross-arch matrix).  Assert the entry branches to the start block.
 */
struct S { int a; int b; };

int use_locals(void) {
    struct S s;
    s.a = 1;
    s.b = 2;
    return s.a + s.b;   /* local stores + loads -> needs stack base */
}

int no_locals(void) {
    return 42;          /* trivial: start block is an empty `b .bb1` */
}
