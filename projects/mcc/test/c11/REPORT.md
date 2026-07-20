# mcc C11 Conformance Report

mcc version: `mcc (MeuOS C Compiler) 0.1.0`

| Feature | Status | Evidence |
| --- | --- | --- |
| `_Atomic` basic | PASS | `atomic_basic.c`: seq_cst load/store, RMW, bitwise fetch, exchange, compare-exchange, flags |
| `_Atomic` concurrent | PASS | `atomic_concurrent.c`: two pthread workers increment to 2000 |
| `_Generic` | PASS | `generic.c`: int and default associations |
| `_Thread_local` | PASS | `thread_local.c`: two concurrent pthread instances retain distinct values |
| `_Alignas` / `_Alignof` | PASS | `alignas.c`, `alignof.c` |
| `_Noreturn` | PASS | `noreturn.c`: noreturn function reaches `exit(0)` |
| `_Static_assert` | PASS | `static_assert.c`: translation-time assertion and runtime PASS |
| Anonymous aggregates | PASS | `anon_struct.c`: promoted anonymous member access |
| Compound literals | PASS | `compound_lit.c`: array compound literal |
| Designated initializers | PASS | `desig_init.c`: out-of-order struct designators |
| VLA | PASS | `vla.c`: runtime-sized stack array |

Run the complete matrix with `make -C mcc check-c11`.

The test-local `stdatomic.h` is a temporary compiler regression shim.  The
installed MeuOS libc header and its runtime implementation remain separate
deliverables.  Fences, lock-free queries, pointer arithmetic, and objects
wider than 64 bits are not covered by the atomic subset.  Host static
link validation is unavailable in this environment because its toolchain has
no static libc/libatomic archives.
