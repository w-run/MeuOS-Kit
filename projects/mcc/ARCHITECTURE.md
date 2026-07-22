# mcc - Directory Structure & Module Index

> Scope of this document: navigation aid for AI agents (and humans)
> working in `mcc/`. For the bootstrap pipeline context see
> `../../AGENTS.md` §3 and `../../STATE.md`. For per-file design notes
> see `../.trae/skills/mkit-bootstrap/SKILL.md`.

## 1. Overview

`mcc` is a single-binary C11 compiler built by source-level integration
of cproc (frontend) and QBE (backend). Per AGENTS.md §2.3, the text-IL
serialization step that originally bridged cproc -> QBE has been
eliminated: the frontend's semantic phase builds the IR `Fn`
**in memory** via `src/irgen/emit.c` and runs the optimization/codegen
pipeline directly.

The directory layout uses a unified `src/` tree (no frontend/backend
split) preserving a logical pipeline:

```
source.c -> src/{lex,parse,sema} -> src/irgen -> src/{ir,opt,abi,emit} + src/target -> asm
```

where each subdirectory corresponds to a pipeline stage, plus `src/driver/`
for the unified `main` and `include/` for shared headers.

## 2. Directory Tree

```
mcc/
├── Makefile                 # simple Makefile (AGENTS.md §4 forbids autotools/cmake)
├── mcc                      # built binary (gitignored)
├── ARCHITECTURE.md          # this file
├── include/                 # shared headers (consumed by every .c below)
│   ├── mcc.h                #   frontend public API (types, decls, exprs, scopes)
│   ├── tokens.h             #   token enum X-macro list
│   ├── util.h               #   list/array/treenode + xmalloc/xreallocarray/noreturn
│   ├── utf.h                #   UTF-8 decoder
│   ├── arg.h                #   argv parser (ARGBEGIN/ARGEND + long-option support)
│   ├── ops.h                #   frontend IR opcode X-macro list (IXXX)
│   ├── ir.h                 #   IR types (Ref/Ins/Blk/Fn/Typ/Con/Dat/Target) + IR-construction API
│   ├── ir_ops.h             #   IR opcode X-macro list (OXXX) + optab fields
│   ├── x86_64.h             #   x86-64 backend interface
│   ├── aarch64.h            #   AArch64 backend interface
│   ├── riscv64.h            #   RISC-V 64 backend interface
│   ├── i386.h               #   i386 backend interface
│   └── loongarch64.h        #   LoongArch64 backend interface
├── src/                     # unified source tree (no frontend/backend split)
│   ├── driver/
│   │   ├── main.c            #   entry point: argv parse -> pp -> decl() loop -> emit
│   │   ├── target_select.c   #   -target triplet -> Target* / canonical name
│   │   ├── host_toolchain.c  #   host assembler/linker handoff (cc -c / cc link)
│   │   ├── usage.c           #   --version / --help text
│   │   ├── arg_compat.c      #   single-dash multi-letter option normalization
│   │   └── driver_internal.h #   shared driver decls (Target externs, prototypes)
│   ├── lex/                 #   C11 frontend: lexer + preprocessor
│   │   ├── scan.c           #     UTF-8 scanner
│   │   ├── token.c          #     token kind table
│   │   ├── pp.c             #     preprocessor (#if/#ifdef/#include/#define + -D/-U/-I API)
│   │   ├── pp_expr.c        #     #if constant-expression arithmetic evaluator (split from pp.c)
│   │   └── pp_internal.h    #     shared decl between pp.c and pp_expr.c (evalconst)
│   ├── parse/               #   C11 frontend: parser
│   │   └── decl.c, expr.c, stmt.c, tree.c, attr.c
│   ├── sema/                #   C11 frontend: semantic analysis
│   │   └── type.c, scope.c, eval.c, init.c, map.c, targ.c
│   ├── util/                #   C11 frontend: shared utilities
│   │   └── util.c, utf.c
│   ├── irgen/               # AST -> IR direct construction (was single irgen.c, now split)
│   │   ├── irgen.h          #   internal shared header (struct value/block/func + helper decls)
│   │   ├── value.c          #   mkblock/mkglobal/mkintconst/mkfltconst/irtype/switchcase
│   │   ├── inst.c           #   functemp/mkinst/funcinst (instruction list builder)
│   │   ├── convert.c        #   funcbits/convert (bitfield + arithmetic conversions)
│   │   ├── funcmem.c        #   calcvla/funcalloc/funccopy/funcstore/funcload
│   │   ├── func.c           #   mkfunc/delfunc/funclabel/funcjmp/funcjnz/funcret/funchlt/funcgoto
│   │   ├── branch.c         #   funclval/funcbranch (lvalue + short-circuit)
│   │   ├── expr.c           #   funcexpr/zero/funcinit (expression + initializer lowering)
│   │   ├── switch.c         #   casesearch/funcswitch (binary-search switch lowering)
│   │   ├── emittype.c       #   emittype (register frontend struct/union into IR typ[] table)
│   │   └── emit.c           #   fe_to_ir_op/valref/run_passes/emitfunc/emitdata
│   ├── ir/                  #   IR core + utilities
│   │   ├── optab.c          #     Op optab[NOp] attribute table
│   │   ├── printfn.c        #     printcon/printref/printfn (debug IL dumper)
│   │   └── ir_util.c        #     emit/newtmp/getcon/newcon/idup/vgrow/alloc/freeall + globals
│   ├── opt/                 #   14 optimization passes:
│   │   ├── cfg.c             #     fillpreds/fillcfg/filldom/fillfron/fillloop/simplcfg
│   │   ├── ssa.c             #     ssa (SSA construction)
│   │   ├── copy.c            #     narrowpars
│   │   ├── fold.c            #     constant folding
│   │   ├── gvn.c             #     global value numbering
│   │   ├── gcm.c             #     global code motion
│   │   ├── simpl.c            #     simplifications
│   │   ├── ifopt.c           #     if-conversion
│   │   ├── mem.c             #     promote (mem2reg) + coalesce
│   │   ├── alias.c           #     alias analysis
│   │   ├── load.c            #     load optimization
│   │   ├── live.c            #     liveness + spill cost
│   │   ├── spill.c           #     register spill
│   │   └── rega.c            #     register allocation
│   ├── abi/abi.c             #   typclass/selcopy/insnew (shared ABI helpers)
│   ├── emit/emit.c           #   emitfn/emitfin/emitdat (generic asm emission glue)
│   └── target/               #   per-arch backends (one subdir per target)
│       ├── x86_64/           #     System V x86-64 target
│       ├── aarch64/          #     same four files for AArch64 (AAPCS64 ABI)
│       ├── riscv64/          #     same four files for RISC-V 64 (lp64d ABI)
│       ├── i386/             #     i386 System V ABI backend
│       └── loongarch64/      #     LoongArch64 LP64D ABI backend
└── build/                   # object files + dep files (gitignored, auto-discovered)
```

## 3. Module Responsibilities

| Directory | Stage | Responsibility |
|-----------|-------|----------------|
| `src/driver/` | orchestration | gcc/clang-style argv parse, init target, run preprocessor, drive `decl()` loop until EOF, emit forward decls, route output (-c/-S/-E/-o), invoke host `cc` for assemble/link |
| `src/lex/` | source -> tokens | UTF-8 scanner, C11 token table, full C preprocessor (`#if`/`#ifdef`/`#ifndef`/`#elif`/`#else`/`#endif`/`#include`/`#define`/`#undef`/`__VA_OPT__`, plus `-D`/`-U`/`-I` API) |
| `src/parse/` | tokens -> AST | recursive-descent parser building `struct decl`/`expr`/`stmt` trees; `attr.c` handles `_Noreturn`/`_Alignas`/`fallthrough` etc. |
| `src/sema/` | AST -> typed AST | type table (`type.c`), scope tracking (`scope.c`), constant folding (`eval.c`), initializer parsing (`init.c`), tree/map helpers, target-specific wchar_t / va_list layout (`targ.c`) |
| `src/irgen/` | typed AST -> IR | the "integration boundary" - direct IR construction via `func*` builders, then `emitfunc` walks the per-function `struct func` CFG and translates to IR `Fn` in memory (no text IL) |
| `src/ir/` | IR utilities | `ir_util.c` provides the in-memory IR construction API (`emit`/`newtmp`/`getcon`/`idup`/`vgrow`); `optab.c` is the operator attribute table; `printfn.c` is a debug IL dumper |
| `src/opt/` | IR -> optimized IR | 14 SSA passes: CFG construction -> SSA -> fold -> GVN -> GCM -> mem2reg -> load opt -> liveness -> spill -> regalloc |
| `src/abi/` | ABI helpers | shared `typclass()` and copy/spill primitives consumed by each target's `abi.c` |
| `src/emit/` | asm emission | target-agnostic `emitfn`/`emitdat` driver that calls `T.emitfn`/`T.emitdat` |
| `src/target/<arch>/` | IR -> asm | directory and file names use canonical architecture IDs. Internal ABI symbols may include an ABI suffix such as `_sysv`. Each backend has a target descriptor, ABI lowering, instruction selection, and GAS emission. |

## 4. Key Files Quick Index

Useful jump points when investigating a specific bug or feature:

- **Adding a new C11 feature** -> start in `src/parse/` (parser), then `src/sema/type.c` (type system), finally `src/irgen/expr.c` (IR lowering)
- **Adding a new IR instruction** -> add to `include/ops.h` (X-macro) -> `src/irgen/expr.c` (emit) -> `src/irgen/emit.c:fe_to_ir_op()` (translate to IR op)
- **mcc segfaults during compile** -> most likely `src/irgen/emit.c:emitfunc()` (Fn init) or `src/ir/ir_util.c:vgrow()` (Vec corruption)
- **Wrong code generated** -> check `src/irgen/emit.c:valref()` (value -> Ref translation), then `src/opt/` passes in `run_passes()` order
- **ABI bug (struct return, varargs)** -> `src/target/<arch>/<arch>_abi.c` or `src/target/x86_64/x86_64_sysv.c`
- **Adding a new target** -> add `src/target/<new>/` with 4 files mirroring `amd64/`, register it in `src/driver/target_select.c:pick_target()`
- **`mcc -S` output wrong** -> `src/target/<arch>/<arch>_emit.c`
- **Preprocessor bug** -> `src/lex/pp.c` (conditional compilation, #include, macro expansion, -D/-U/-I); `src/lex/pp_expr.c` (#if arithmetic evaluation)
- **Command-line option not working** -> `src/driver/main.c` (argv parsing) + `src/driver/arg_compat.c` (option normalization) + `include/arg.h` (ARGBEGIN macro)
- **Phase 1a regression** -> `make check` (must print `exit=0`)
- **IR pass ordering** -> `src/irgen/emit.c:run_passes()` mirrors `reference/qbe/main.c`'s `func()` callback

## 5. irgen/ Split Rationale

The original `irgen.c` was a single 1623-line file mixing:
data-structure definitions, value/block construction, instruction
building, type conversions, memory ops, function control-flow,
branches, expression lowering, switch lowering, and IR emission.

After the Phase B refactor it is split into 10 files averaging ~150
lines each, plus an internal shared header `irgen.h`. The split
criteria was **functional cohesion** - each file owns one phase of
the AST -> IR translation:

| File | Owns | Exports to |
|------|------|------------|
| `value.c` | value/block construction | all of irgen/, frontend |
| `inst.c` | instruction list builder | all of irgen/ (via `funcinst`) |
| `convert.c` | arithmetic/bitfield conversions | funcmem/, expr/ |
| `funcmem.c` | stack alloc + load/store + VLA | expr/ (via `funcstore`/`funcload`/`calcvla`) |
| `func.c` | function entry + control flow | frontend (mkfunc etc.) |
| `branch.c` | lvalue + short-circuit | expr/ (via `funclval`/`funcbranch`) |
| `expr.c` | expression + initializer lowering | frontend (via `funcexpr`/`funcinit`) |
| `switch.c` | switch statement lowering | frontend (via `funcswitch`) |
| `emittype.c` | register struct/union into IR `typ[]` | all of irgen/ (via `emittype`, called from `mkfunc`/`funcexpr`) |
| `emit.c` | IR construction + pass pipeline | frontend (via `emitfunc`/`emitdata`) |

`irgen.h` is **internal to `src/irgen/`**: it declares the `struct value`/
`block`/`func` types and the cross-file helpers (`mkfltconst`,
`irtype`, `functemp`, `funcinst`, `funcbits`, `convert`, `calcvla`,
`funcalloc`, `funcstore`, `funcload`, `funclval`). The frontend
itself only sees the public API in `mcc.h` (`mkfunc`, `funcexpr`,
`funcinit`, `emitfunc`, `emitdata`, ...).

## 6. Build System

Per AGENTS.md §4, mcc is built with a **simple Makefile** (no
autotools/cmake/meson). Key Makefile mechanics:

- `SRC_DIRS := src` - single root; `find` recursively auto-discovers
  all `.c` files. **Adding/removing a `.c` file requires no Makefile
  edit.**
- All five target backends (`x86_64`, `aarch64`, `riscv64`, `i386`,
  `loongarch64`) are always linked into a single `mcc` binary; runtime target selection is via
  `pick_target()` in `src/driver/target_select.c` (consults `-target`/`-t` flag
  or host `uname -m`).
- Build artifacts go to `build/` (mirrors source layout), which is
  gitignored along with the `mcc` binary.
- `CFLAGS = -O2 -g -std=gnu11 -Iinclude -Wno-all` - `-Wno-all` is
  intentional; the integrated cproc/QBE source emits heavy `-Wall`
  noise (unused params, sign compares, missing braces in designated
  initializers). Warning hygiene will be revisited once mcc can
  self-host (Phase 4).
- `make check` runs the Phase 1a gate: `int main(void){return 0;}`
  must compile, link, run, and exit 0.

## 7. Phase Status

See `../../STATE.md` for the canonical status. Quick reference:

| Phase | Status | Gate |
|-------|--------|------|
| 0 - prep | PASS | `gcc` available, `MEUOS_SYSROOT` set |
| 1a - hello | PASS | `make check` exits 0 |
| 1b - control flow + call | PASS | fib(10)=55, for/while, gcd all exit 0 |
| 1c - compound types | PASS | strlen + struct pass/return (small/large/nested) + union + global init, 9/9 exit 0 |
| 1d - C11 features | PASS (mcc host gate) | `make check-c11`: 12 runtime tests |
| 2 - meuos-libc | not started | installed headers, crt objects, atomic runtime |
| 3 - meow | blocked on 2 | `meow build dash` |
| 4 - bootstrap | blocked on 3 | chroot self-rebuild |

## 8. Progressive Cleanup Notes

The user has asked for **progressive** removal of `cproc`/`qbe` naming
artifacts and structural integration of the two source trees. Status:

**Done in Phase B (irgen/ split round)**:
- `cproc_op_to_qbe()` -> `fe_to_ir_op()` (in `src/irgen/emit.c`)
- Comment cleanup: most "cproc does X" -> "the frontend does X"
- File split removed monolithic `irgen.c` (no cproc/qbe filename in `src/irgen/`)

**Done in Phase 1b (control flow + call round)**:
- Pass 2 emit pattern switched from BACKWARD `emit()` to FORWARD
  `*curi++` writes - eliminates instruction-order inversion bug.
- `err()` redefined in `src/ir/ir_util.c` - was originally in
  `parse.c` (deleted in Phase B). Without the definition the linker
  resolved it to libc's `err(int eval, ...)` with a different signature.
- `Opar` emission added to start of entry block.
- Phi node construction (`b->phi` -> IR `Phi`) for if-else result merging.
- `ICALL` handler: collects `IARG*`/`IVARARG`, emits `Oarg*` then
  `Ocall`, sets `fn->leaf = 0`.

**Done in Phase 1c (compound types round)**:
- `emitdata()` implemented (was Phase 1a stub): walks `struct init`
  list, evals each expr, builds `Lnk`, emits `Dat` sequence via
  `emitdat()`. Helpers: `globalname(v)` + `escape_string()`.
- `emittype.c` new file (~180 lines): registers frontend struct/union
  types into IR's `typ[]` table.
- `irgen.h`: `emittype` promoted from `static inline` no-op to real
  declaration `void emittype(struct type *t);`.
- `valref()` VALUE_TYPE case: `return TYPE(v->id);`.
- `fn->slot` initialization fixed from `-1` to `0`.
- Aggregate parameter/argument/return/call-return support (Oparc/Oargc/
  Jretc + ICALL arg[1]=VALUE_TYPE).
- **Verification (9/9 exit 0)**: strlen + fib + control flow + global
  init + pointer arithmetic + struct1-4 (small/large/nested/union).

**Done in Phase 1c-2 refactor (structural integration round)**:
- **Header renames** (de-qbe identification):
  - `include/qbe.h` -> `include/ir.h` (IR data structures)
  - `include/qbe_ops.h` -> `include/ir_ops.h` (IR opcode X-macro)
  - `include/cc.h` -> `include/mcc.h` (frontend public API)
  - `backend/ir/qbe_util.c` -> `src/ir/ir_util.c`
- **Directory restructure** to unified `src/` tree (no
  frontend/backend split):
  - `driver/` -> `src/driver/`
  - `frontend/{lex,parse,sema,util}/` -> `src/{lex,parse,sema,util}/`
    (removed `frontend/` intermediate layer)
  - `irgen/` -> `src/irgen/`
  - `backend/{ir,opt,abi,emit}/` -> `src/{ir,opt,abi,emit}/`
    (removed `backend/` intermediate layer)
  - `target/` -> `src/target/` (with amd64/arm64/rv64 subdirs)
- **Makefile**: `SRC_DIRS := src` (single recursive root)
- **Symbol renames** (internal API, no public ABI impact):
  - `qbetype()` -> `irtype()` (in `src/irgen/value.c`)
  - `b->qbe` -> `b->ir` (struct block field, IR Blk pointer)
  - `run_qbe_passes()` -> `run_passes()` (in `src/irgen/emit.c`)
  - `ir_to_qbe_op()` -> `fe_to_ir_op()` (in `src/irgen/emit.c`)
- **Comment cleanup**: "QBE" -> "IR" / "backend" (in src/ and include/
  .c/.h files, 78 files touched); "cproc" -> "the frontend" where
  referring to our own code. **Preserved**: `reference/cproc/` and
  `reference/qbe/` path references (accurate provenance to external
  reference trees), code symbols that are part of public API contracts.
- **Verification**: 9/9 Phase 1c regression tests still exit=0.

**Done in Phase 1c-3 (command-line modernization)**:
- **gcc/clang-style argument parsing** in `src/driver/main.c`:
  - Output: `-o`, `-c` (compile to .o, no link), `-S`, `-E`
  - Preprocessor: `-D<macro>[=<val>]`, `-U<macro>`, `-I<dir>`,
    `-nostdinc`, `-M`/`-MM`/`-MD`/`-MMD` (dependency generation),
    `-P`, `-H`
  - Linker: `-L<dir>`, `-l<lib>`, `-static`, `-nostdlib`,
    `-nodefaultlibs`, `-pie`/`-fPIE`/`-fpic`/`-fPIC`
  - Language: `-std=<standard>`, `-f<feature>`
  - Diagnostics: `-O<level>`, `-g`, `-w`, `-W<warning>`
  - Target: `-target <triplet>` (replaces `-t`; `-t` kept as alias),
    `-m<arch-option>`
  - Other: `-v` (verbose), `-pipe`, `-pedantic`, `--version`,
    `--help`, `--specs=meuos`, `--sysroot=<dir>`
  - `--shared`: emits a shared object through the host linker.
  - `--specs=meuos`: selects the supplied MeuOS sysroot, its `crt1.o`, and
    `libc-meuos.a` without falling back to the host C runtime.
- **`include/arg.h`**: ARGBEGIN macro extended with `case '-'` to
  dispatch long options (`--version`, `--help`) while staying
  backward-compatible with short-option cluster parsing.
- **`src/lex/pp.c`**: implemented previously-missing preprocessor
  directives `#if`/`#ifdef`/`#ifndef`/`#elif`/`#else`/`#endif`
  (conditional compilation stack + skip logic), `#include` (with -I
  path search + quote/angle-bracket distinction), `#if` constant
  expression evaluator (with `defined`, macro expansion, recursive
  descent). Public API: `ppdefine()`/`ppundef()`/`ppincludepath()`/
  `ppdumpdeps()`.
- **`-target` triplet mapping**: public target identifiers are
  `x86_64`, `aarch64`, `riscv64`, `i386`, and `loongarch64`.
  Historical aliases (`amd64`, `arm64`, `rv64`, `la64`) remain accepted.
  `targ_name()` in `src/driver/target_select.c` maps
  full triplets (e.g. `x86_64-unknown-linux`) to frontend's expected
  short names (e.g. `x86_64-sysv`), without modifying `targ.c`.
- **`-c` output routing**: asm to temp file, then `cc -c` to produce
  `.o` (no link). Default output `<input>.o` when no `-o`.
- **Verification**: `make check`, `make check-c11`,
  `make check-loongarch64`, and `make check-driver` pass.  The Phase 1
  bootstrap installs mcc into the sysroot and reruns hello-world plus the
  two-thread atomic gate with that installed binary.

**Still pending (future rounds)**:
- `qbe_ops.h` T() macro enum (`Ksb`/`Kub`/...) in `src/ir/optab.c`
  - file-local enum copied verbatim from `reference/qbe/parse.c` L5-15
  (semantically a QBE artifact, but renaming would require touching
  every reference to Ksb/Kub/Ksh/Kuh across the backend; deferred).
- Comments throughout `src/` and `src/target/` that reference QBE
  source files by name (these are accurate provenance notes and may
  be worth keeping for cross-referencing during the bootstrap audit).
- Implement true `-O` level control (mcc currently always optimizes).
- Implement warning system for `-W`/`-w` (currently accepted but no-op).
- i386 64-bit mul/div/rem (`Omul/Odiv/Orem/Oudiv/Ourem Kl`): solved via
  `i386_sysv_abi()` pre-pass that rewrites to libc soft-arith calls
  (see `.todo/i386-kl-arith.md` for implementation details).
- Extend shared-library validation beyond the x86_64 host DSO/TLS runtime
  regression to target runtime/linker integration once each target has a
  MeuOS sysroot.

The principle is: **rename symbols whose cproc/qbe origin is not
semantically meaningful** (e.g. internal helper names), but **keep
references that document algorithmic provenance** (e.g. "mirrors
qbe/main.c's func() callback") since those help anyone reading the
reference trees understand the mapping.

## 9. Reference Trees

Read-only reference source lives in `../reference/` (gitignored):

| Path | Version | Commit | Used for |
|------|---------|--------|----------|
| `reference/cproc/` | HEAD | `d1c53ddf` | frontend design reference |
| `reference/qbe/` | v1.3 | `c0818978` | backend design reference |
| `reference/musl/` | v1.2.6 | `9fa28ece` | meuos-libc algorithm reference (not used by mcc) |

When investigating an IR pass behavior, the canonical source is
`reference/qbe/<file>.c`. The corresponding mcc copy lives in
`mcc/src/opt/<file>.c` (or `mcc/src/ir/`, `mcc/src/emit/`).
cproc frontend files mirror 1:1 to `mcc/src/{lex,parse,sema,util}/`.
