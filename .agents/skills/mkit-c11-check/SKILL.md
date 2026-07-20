---
name: "mkit-c11-check"
description: "Validates mcc's C11 conformance via a test program matrix. Invoke when user asks to test mcc C11 features (_Atomic, _Generic, _Thread_local, _Alignas, anonymous structs, VLAs, etc.) or after parser/codegen changes."
---

# mcc C11 Conformance Tester

Runs a battery of self-contained test programs through `mcc` to verify C11 feature support as required by AGENTS.md §2.3.

## When to invoke

- User asks to "test mcc C11 support" or "check if mcc handles `_Atomic` / `_Generic` / etc."
- After changes to mcc's lexer, parser, type checker, or codegen
- During Phase 1 bootstrap validation (extended C11 coverage)
- User asks "what C11 features does mcc support"
- User asks "is mcc ready for Phase 2"

## Test matrix

Each test is a self-contained `.c` file under `mcc/test/c11/<feature>.c`. Each must print `PASS` on success and exit 0. Failure modes: (a) mcc refuses to compile, (b) compilation succeeds but runtime output wrong, (c) exit code != 0.

| Feature                 | Test file             | Expected behavior                                                                    |
| ----------------------- | --------------------- | ------------------------------------------------------------------------------------ |
| `_Atomic` (basic)       | `atomic_basic.c`      | `_Atomic int x = 0; atomic_fetch_add(&x, 1);` → x == 1                               |
| `_Atomic` (concurrent)  | `atomic_concurrent.c` | The AGENTS.md §6 Task 1 test: 2 threads × 1000 increments → `counter = 2000`, exit 0 |
| `_Generic`              | `generic.c`           | `_Generic((x), int: 1, default: 0)` selects correct branch for each input type       |
| `_Thread_local`         | `thread_local.c`      | Each thread sees its own value of `thread_local int x`                               |
| `_Alignas`              | `alignas.c`           | `char _Alignas(64) x;` has `&x % 64 == 0`                                            |
| `_Alignof`              | `alignof.c`           | `_Alignof(long long) >= 8`                                                           |
| `_Noreturn`             | `noreturn.c`          | `_Noreturn void f() { exit(0); }` — function does not return                         |
| `_Static_assert`        | `static_assert.c`     | Compile-time assertion passes for valid expression                                   |
| Anonymous struct/union  | `anon_struct.c`       | `struct { struct { int a; }; } s; s.a = 1;`                                          |
| Compound literals       | `compound_lit.c`      | `int *p = (int[]){1, 2, 3};` — `p[2] == 3`                                           |
| Designated initializers | `desig_init.c`        | `struct foo f = {.a = 1, .b = 2};` — fields set correctly                            |
| Variable-length arrays  | `vla.c`               | `int n = 5; int v[n]; for (...) v[i] = i;` — sum correct                             |

## Workflow

1. Verify `mcc` binary exists at `${MEUOS_SYSROOT}/usr/bin/mcc` (or `build/mcc`). If not, abort and suggest running `mkit-bootstrap` Phase 1 first.
2. For each test file in `mcc/test/c11/*.c`:
   a. Compile: `mcc -specs=meuos -static -o /tmp/c11_<feature>.bin mcc/test/c11/<feature>.c`
   b. Capture compiler stderr (compile errors block the run).
   c. Run: `/tmp/c11_<feature>.bin` — capture stdout, exit code.
   d. Mark **PASS** if exit code is 0 AND stdout contains `PASS`.
   e. Otherwise mark **FAIL** with the captured output.
3. Print summary table: feature | pass/fail | notes.
4. Map failures to AGENTS.md §2.3 hard requirements (see below).

## AGENTS.md §2.3 hard requirements (all MUST pass)

> **C11 支持**：必须完整支持 `_Atomic`、`_Generic`、`_Thread_local`、`_Alignas`、`_Alignof`、`_Noreturn`、`_Static_assert`、匿名结构体/联合体、复合字面量、指定初始化器、变长数组。

A failure on any of these blocks Phase 1 validation (the §6 Task 1 test program uses `_Atomic` + threads).

## Output: `mcc/test/c11/REPORT.md`

After each run, write a report with:

```markdown
# mcc C11 Conformance Report

Generated: <ISO timestamp>
mcc version: <mcc -v output>
mcc binary: <path>

| Feature               | Status | Notes                                          |
| --------------------- | ------ | ---------------------------------------------- |
| \_Atomic (basic)      | PASS   | —                                              |
| \_Atomic (concurrent) | FAIL   | compile error: "unsupported type \_Atomic int" |
| \_Generic             | PASS   | —                                              |
| ...                   | ...    | ...                                            |

## Summary

X / 12 passed.

## Failures and suggested fixes

### \_Atomic (concurrent)

- Compiler output: ...
- Likely cause: parser/type checker missing `_Atomic` qualifier handling.
- Suggested file: `mcc/src/type.c` (apply_atomic_qualifier function).
- AGENTS.md reference: §2.3 (hard requirement).
```

## Test file template

Each test file follows this skeleton:

```c
#include <stdio.h>
#include <stdatomic.h>
#include <threads.h>

/* feature-specific test body */

int main(void) {
    /* run test */
    if (condition_holds) {
        printf("PASS\n");
        return 0;
    }
    printf("FAIL\n");
    return 1;
}
```

## Edge cases to add as tests grow

- Atomic ops on different types (`_Atomic int`, `_Atomic long`, `_Atomic void *`)
- `_Generic` with default branch and 5+ concrete branches
- `_Thread_local` with non-trivial destructor behavior
- Nested anonymous structs (3+ levels)
- VLAs in loops (allocation/deallocation)
- Compound literals in `static` initializers (must be a constant expression in C11)

## Don't do

- Don't run tests against the host `gcc` — only `mcc`. (Comparing mcc output against gcc is a separate `mkit-il-diff` skill concern.)
- Don't skip the concurrent atomic test — it's the AGENTS.md §6 Task 1 validation program.
- Don't consider a test "passing" if it exits non-zero, even if stdout contains `PASS`.
