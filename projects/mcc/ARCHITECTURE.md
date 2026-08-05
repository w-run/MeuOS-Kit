# mcc - Directory Structure & Module Index

> Scope of this document: navigation aid for AI agents (and humans)
> working in `mcc/`. For the bootstrap pipeline context see
> `../../AGENTS.md` §3. For skill-based bootstrap orchestration see
> `.codebuddy/skills/mkit-bootstrap/SKILL.md`.

## 1. Overview

`mcc` is now a **shared-backend multi-language compiler** (gcc-style).
It started as a single-binary C11 compiler built by source-level
integration of cproc (frontend) and QBE (backend); the 2026-08
restructure (branch `worktree-mxx-work`, tracked in `.issues/0802.md`)
split the backend into `libmcc.a` and added a C++ frontend (`m++`):

- **C frontend**: `src/{lex,parse,sema,irgen}` — C11 lex/parse/sema +
  AST -> `MFn` (MIR) lowering (`src/c/irgen/func_to_mir.c`).
- **C++ frontend**: `src/cpp/{lex,parse}` — the `m++` binary
  (`g_lang=1`, driver `src/driver/mpp_main.c`). It lowers C++ constructs
  (class/namespace/template/overload/…) onto the same C decl/expr/AST
  machinery, then feeds the shared MIR path. Both binaries link
  `build/libmcc.a`.
- **MIR core**: `src/mir/` — `MFn` intermediate (MType/MVal/MConst/MIns)
  plus passes (`mfold`/`msimpl`/`mdce`/`mcopy`/`mgvn`).
- **Machine backend**: each `src/target/<arch>/<arch>_mabi.c` is a
  machine-level `MFnM` lowering (`src/mir/{machine,regalloc}.c` +
  `src/target/<arch>/<arch>_m*.c`); the regalloc/machine layer converts
  the optimized `MFn` to a machine `MFnM`, and the target's `_memit.c`
  emits the assembly. The legacy LIR bridge (`src/lir/bridge.c`) and the
  `MCC_MIR_BACKEND` env override were both removed (Phase 2 / Phase 3e) —
  the MIR machine backend is now the sole asm producer.

Default pipeline (`g_use_mir = 1`; `MCC_USE_MIR` env has been removed, see
`src/driver/main.c`):

```
source.c  -> src/c/{lex,parse,sema} -> src/c/irgen ----+
                                                     v
source.cc -> src/cpp/{lex,parse} ----------------> MFn (src/mir) -> MIR passes
                                                        |
                                                        v
                              regalloc/machine (src/mir) -> <arch>_memit -> asm
```

The directory layout uses a unified `src/` tree preserving a logical
pipeline, plus `src/driver/` for the per-language drivers (`c_main.c` /
`mpp_main.c`) and `include/` for shared headers.

## 2. Directory Tree

```
mcc/
├── Makefile                 # simple Makefile (AGENTS.md §4 forbids autotools/cmake)
├── mcc                      # C driver binary (gitignored)
├── m++                      # C++ driver binary, links libmcc.a (gitignored)
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
│   ├── mir.h                #   MIR core API (MType/MVal/MConst/MIns/MFn/MFnM)
│   ├── cpp.h                #   C++ frontend API (m++ symbols, g_lang=1)
│   ├── cpp/cpp_tokens.h     #   C++ keyword/token tables (C++98/11/20)
│   ├── x86_64.h             #   x86-64 backend interface
│   ├── aarch64.h            #   AArch64 backend interface
│   ├── riscv64.h            #   RISC-V 64 backend interface
│   ├── i386.h               #   i386 backend interface
│   ├── loongarch64.h        #   LoongArch64 backend interface
│   └── arm.h                #   ARM 32-bit backend interface
├── src/                     # unified source tree (no frontend/backend split)
│   ├── driver/
│   │   ├── main.c            #   entry point: argv parse -> pp -> decl() loop -> emit
│   │   ├── c_main.c          #   C-mode main() wrapper (g_lang=0), linked into mcc
│   │   ├── mpp_main.c        #   C++-mode main() wrapper (g_lang=1), linked into m++
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
│   ├── util/                #   shared utilities (used by both frontends + backend)
│   │   └── util.c, utf.c
│   ├── cpp/                 #   C++ frontend (m++ binary; lowers C++ -> C decl/expr AST)
│   │   ├── lex/cpp_scan.c   #     C++ tokenizer (keyword table + classification test)
│   │   └── parse/cpp_parse.c#     C++ parser: class/namespace/template/overload/
│   │                         #       ctor/dtor/virtual lowering + mangled names
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
│   │   ├── func_to_mir.c    #   func -> MFn (MIR) lowering (MIR-path entry, g_use_mir=1)
│   │   └── emit.c           #   fe_to_ir_op/valref/run_passes/emitfunc/emitdata
│   ├── mir/                 #   MIR core (shared intermediate, MFn) + machine/regalloc layer
│   │   ├── build.c          #     MFn/MType/MVal/MConst construction API (arena)
│   │   ├── passes.c         #     build_uses + mfold/msimpl/mdce + run_mir_passes()
│   │   ├── copy.c           #     mcopy copy-propagation pass
│   │   ├── gvn.c            #     mgvn global-value-numbering pass
│   │   ├── machine.c        #     machine-level MInsM/MFnM (machine-backend input)
│   │   ├── regalloc.c       #     linear-scan regalloc (calls crossing / hint / spills)
│   │   ├── mir_util.c       #     arena / const-pool helpers
│   │   └── print.c          #     -dmir MFn dumper
│   ├── ir/                  #   IR core + utilities
│   │   ├── optab.c          #     Op optab[NOp] attribute table
│   │   ├── printfn.c        #     printcon/printref/printfn (debug IL dumper)
│   │   └── ir_util.c        #     emit/newtmp/getcon/newcon/idup/vgrow/alloc/freeall + globals
│   ├── opt/                 #   14 optimization passes (legacy LIR backend):
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
│   └── target/               #   per-arch machine backends (one subdir per target)
│       ├── x86_64/           #     System V x86-64 target
│       │   ├── x86_64_mabi.c #       MIR machine-backend ABI (mfnm_abi_x86_64)
│       │   ├── x86_64_mbe.c  #       MIR machine-backend driver (mfnm_backend_x86_64)
│       │   └── x86_64_memit.c#       MIR machine-backend asm emission
│       ├── aarch64/          #     same three files for AArch64 (AAPCS64 ABI)
│       ├── riscv64/          #     same three files for RISC-V 64 (lp64d ABI)
│       ├── i386/             #     i386 System V ABI backend
│       ├── loongarch64/      #     LoongArch64 LP64D ABI backend
│       └── arm/              #     ARM 32-bit (v7+ with VFP) AAPCS ABI backend
└── build/                   # object files + dep files + libmcc.a (gitignored)
```

## 3. Module Responsibilities

| Directory | Stage | Responsibility |
|-----------|-------|----------------|
| `src/driver/` | orchestration | gcc/clang-style argv parse, init target, run preprocessor, drive `decl()` loop until EOF, emit forward decls, route output (-c/-S/-E/-o), invoke host `cc` for assemble/link |
| `src/c/lex/` | source -> tokens | UTF-8 scanner, C11 token table, full C preprocessor (`#if`/`#ifdef`/`#ifndef`/`#elif`/`#else`/`#endif`/`#include`/`#define`/`#undef`/`__VA_OPT__`, plus `-D`/`-U`/`-I` API) |
| `src/c/parse/` | tokens -> AST | recursive-descent parser building `struct decl`/`expr`/`stmt` trees; `attr.c` handles `_Noreturn`/`_Alignas`/`fallthrough` etc. |
| `src/c/sema/` | AST -> typed AST | type table (`type.c`), scope tracking (`scope.c`), constant folding (`eval.c`), initializer parsing (`init.c`), tree/map helpers, target-specific wchar_t / va_list layout (`targ.c`) |
| `src/cpp/` | C++ source -> typed AST (m++) | C++ lexer (`lex/cpp_scan.c`) + parser (`parse/cpp_parse.c`); lowers class/namespace/template/overload/ctor-dtor/virtual to the C decl/expr machinery with mangled names |
| `src/c/irgen/` | typed AST -> MFn | the "integration boundary" - direct IR construction via `func*` builders; `func_to_mir.c` lowers to MIR `MFn` (MIR path, `g_use_mir=1`); `emit.c` runs the MIR passes then dispatches to the target's machine backend (`mfnm_backend_<arch>`) |
| `src/mir/` | typed AST -> MFn + MIR passes | MIR core construction (`build.c`), passes (`passes.c` mfold/msimpl/mdce, `copy.c` mcopy, `gvn.c` mgvn), machine layer + linear-scan regalloc (`machine.c`/`regalloc.c`) feeding each target's `_memit.c` |
| `src/ir/` | IR utilities | `ir_util.c` provides the in-memory IR construction API (`emit`/`newtmp`/`getcon`/`idup`/`vgrow`); `optab.c` is the operator attribute table; `printfn.c` is a debug IL dumper |
| `src/opt/` | IR -> optimized IR | 14 SSA passes: CFG construction -> SSA -> fold -> GVN -> GCM -> mem2reg -> load opt -> liveness -> spill -> regalloc |
| `src/abi/` | ABI helpers | shared `typclass()` and copy/spill primitives consumed by each target's `abi.c` |
| `src/emit/` | asm emission | target-agnostic `emitfn`/`emitdat` driver that calls `T.emitfn`/`T.emitdat` |
| `src/target/<arch>/` | IR -> asm | directory and file names use canonical architecture IDs. Internal ABI symbols may include an ABI suffix such as `_sysv`. Each backend has a target descriptor, ABI lowering, instruction selection, and GAS emission. |

## 4. Key Files Quick Index

Useful jump points when investigating a specific bug or feature:

- **Adding a new C11 feature** -> start in `src/c/parse/` (parser), then `src/c/sema/type.c` (type system), finally `src/c/irgen/expr.c` (IR lowering)
- **Adding a new IR instruction** -> add to `include/ops.h` (X-macro) -> `src/c/irgen/expr.c` (emit) -> `src/c/irgen/emit.c:fe_to_ir_op()` (translate to IR op)
- **mcc segfaults during compile** -> most likely `src/c/irgen/emit.c:emitfunc()` (Fn init) or `src/ir/ir_util.c:vgrow()` (Vec corruption)
- **Wrong code generated** -> check `src/c/irgen/emit.c:valref()` (value -> Ref translation), then `src/opt/` passes in `run_passes()` order
- **ABI bug (struct return, varargs)** -> `src/target/<arch>/<arch>_abi.c` or `src/target/x86_64/x86_64_sysv.c`
- **Adding a new target** -> add `src/target/<new>/` with 4 files mirroring `amd64/`, register it in `src/driver/target_select.c:pick_target()`
- **`mcc -S` output wrong** -> `src/target/<arch>/<arch>_emit.c`
- **Preprocessor bug** -> `src/c/lex/pp.c` (conditional compilation, #include, macro expansion, -D/-U/-I); `src/c/lex/pp_expr.c` (#if arithmetic evaluation)
- **Command-line option not working** -> `src/driver/main.c` (argv parsing) + `src/driver/arg_compat.c` (option normalization) + `include/arg.h` (ARGBEGIN macro)
- **Phase 1a regression** -> `make check` (must print `exit=0`)
- **IR pass ordering** -> `src/c/irgen/emit.c:run_passes()` mirrors `reference/qbe/main.c`'s `func()` callback

## 5. irgen/ Split Rationale

The original `irgen.c` was a single 1623-line file mixing:
data-structure definitions, value/block construction, instruction
building, type conversions, memory ops, function control-flow,
branches, expression lowering, switch lowering, and IR emission.

After the irgen-refactor refactor it is split into 10 files averaging ~150
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

`irgen.h` is **internal to `src/c/irgen/`**: it declares the `struct value`/
`block`/`func` types and the cross-file helpers (`mkfltconst`,
`irtype`, `functemp`, `funcinst`, `funcbits`, `convert`, `calcvla`,
`funcalloc`, `funcstore`, `funcload`, `funclval`). The frontend
itself only sees the public API in `mcc.h` (`mkfunc`, `funcexpr`,
`funcinit`, `emitfunc`, `emitdata`, ...).

## 6. Build System

Per AGENTS.md §4, mcc is built with a **simple Makefile** (no
autotools/cmake/meson). Key Makefile mechanics:

- Sources are split into `FE_DIRS` (C/C++ frontends:
  `src/driver src/cpp src/c`) and
  `BE_DIRS` (shared backend: `src/mir src/ir src/opt src/abi
  src/emit src/target src/util`); `find` auto-discovers `.c` files in
  both, so **adding/removing a `.c` file requires no Makefile edit.**
- Backend objects are archived into `build/libmcc.a`. Two binaries
  link it: `mcc` = C frontend + `src/driver/c_main.c` (g_lang=0);
  `m++` = C++ frontend + `src/driver/mpp_main.c` (g_lang=1).
- All six target backends (`x86_64`, `aarch64`, `riscv64`, `i386`,
  `loongarch64`, `arm`) are always linked into the shared backend; runtime target selection is via
  `pick_target()` in `src/driver/target_select.c` (consults `-target`/`-t` flag
  or host `uname -m`).
- Build artifacts go to `build/` (mirrors source layout), which is
  gitignored along with the `mcc`/`m++` binaries.
- `CFLAGS = -O2 -g -std=gnu11 -Iinclude -Wno-all` - `-Wno-all` is
  intentional; the cproc/QBE-derived source emits heavy `-Wall`
  noise (unused params, sign compares, missing braces in designated
  initializers). Warning hygiene will be revisited once mcc can
  self-host (Phase 4).
- `make check` runs the Phase 1a gate: `int main(void){return 0;}`
  must compile, link, run, and exit 0. Backend/`m++` gates:
  `check-mir` (types/passes/machine/abi/regalloc),
  `check-cpp` (lex/virtual/func/neg), `check-all` (`test/verify-all.sh`).

## 7. Phase Status

See `../../AGENTS.md` §3 for the canonical status. Quick reference:

| Phase | Status | Gate |
|-------|--------|------|
| 0 - prep | PASS | `gcc` available, `MEUOS_SYSROOT` set |
| 1a - hello | PASS | `make check` exits 0 |
| 1b - control flow + call | PASS | fib(10)=55, for/while, gcd all exit 0 |
| 1c - compound types | PASS | strlen + struct pass/return (small/large/nested) + union + global init, 9/9 exit 0 |
| 1d - C11 features | PASS (mcc host gate) | `make check-c11`: 13 runtime tests |
| 1e - C23 features | PASS (mcc host gate) | `make check-c23`: 14 runtime tests |
| 2 - meuos-libc | PASS (6 arch runtime) | `make -C projects/meuos-libc check` (x86_64 full; aarch64/arm qemu; riscv64/loongarch64/i386 bootstrap) |
| 3 - meow | PASS | `meow build bzip2` (pure YAML); `make -C projects/meow check` |
| 4 - bootstrap | PASS | `check-sysroot-static`: sysroot 内自重建全部 `src/` 源码 + libmcc.a + mcc/m++ 链接（0802 记录 self-mcc 105/105 编译通过，见 `.issues/0802.md`） |
| 5 - toolchain | PASS | `check-mt-integration`: MT_AS/MT_LD 集成，零宿主 cc 依赖 |
| 6 - buildtools | PENDING | meuos-buildtools (m4/bison/flex/gperf) 待启动 |
| 7 - userspace | PENDING | meuos-utils/meuos-shell 待启动 |

### m++ / MIR 重构进度（worktree-mxx-work，2026-08）

在 bootstrap 阶段线之外，mcc 正并行执行「共享后端 + 多语言前端」重构。
权威进度记录在 `.issues/0802.md`（随每个技术提交同步更新）。里程碑速览：

| 里程碑 | 状态 | 说明 |
|:-------|:-----|:-----|
| M1 MIR 定型 | ✅ | B.1 MIR 核心（0fc8f8c）+ B.2 首批 passes（220086c）+ B.4 bridge 打通（753b00b） |
| M2 C 迁移 | ✅ | `func_to_mir` 打通，`MCC_USE_MIR=1` 全源码编译通过 |
| M3 m++ hello | ✅ | C.1 骨架 + 双二进制 mcc/m++ 共享 libmcc.a（3958de5） |
| C.2.3 类/继承/重载/构造 | ✅ | 类、成员函数、构造/析构、访问控制、命名空间、单/多继承、运算符重载、引用、静态成员、嵌套类、临时对象 |
| C.2.5 虚函数与虚表 | ✅ | vptr/vtable 布局 + 虚调用降级 + 多态继承（7442b6c） |
| C.2.8 函数模板 | ✅ | instantiate-on-first-use（84727a6） |
| C.2.8 类模板 | ✅ | `Foo<T>` 类型上下文实例化（642574b） |
| C.2.8 成员模板 | ✅ | 类内模板方法 + 类模板实例内的成员模板 `obj.get<int>()`（c93d5f7） |
| auto/decltype | ✅ | auto 变量声明（局部/全局）+ C++14 auto 返回类型推导，支持模板结果/链式/成员模板（160e2a2） |
| 变参模板/包展开 | ✅ | `typename... Args` 包参数 + `f(args...)` 转发 + `sizeof...(Args)` + 空包（df0c489；2eaa662a 修 pack deduce：显式实参/调用实参落入 pack 的计数，`count<int,double>()` 现正确返回 2） |
| lambda（匿名类降级） | ✅ | 闭包合成文件作用域 `__lambdaN` 类，值捕获=成员+合成构造、体=operator()、`obj(args)` 降级（877beed） |
| constexpr 求值器 | ✅ | constexpr 函数体 token 缓冲回放 + 编译期折叠、constexpr 变量常量初值捕获、static_assert 编译期求值（3ac233b；阶段 1 + 阶段 2 的 static_assert 部分） |
| 移动语义（右值引用） | ✅ | `T&&` + 引用折叠 + lvalue/rvalue 值类别重载（4491a27） |
| C++14/17 五项 | ✅ | 泛型 lambda（a28b0f5）、if constexpr（11d919d）、CTAD（a76e7ff）、结构化绑定+内联变量（70890fa） |
| C++20 三项 | ✅ | 三向比较 `<=>`（34d0566）、consteval 立即函数（e698f37）、concepts/requires（8e07d08）+ 概念组合 `&&/||/!` 递归求值（b2da695）——**C++ 98~23 覆盖收官** |
| m++ 边界 4 项 | ✅ | override/final（40d46f2）、ctor 初始化列表（1eb76da）、限定成员调用 `Base::get()`（35a6ede）、new/delete（ecc42cf）+ 数组形式 `new T[n]`/`delete[]`（329de75） |
| new `{args}` braced-init | ✅ | `new T{...}`：标量 value-init、聚合逐成员、用户 ctor 构造已支持；标量数组 `new int[n]{...}` 逐个赋值 + 超出列表 value-init 补 0（含空列表 `{}`）已支持；**类元素数组 `new Pt[n]{...}`**：聚合逐成员 + 用户 ctor 逐元素构造已支持，短列表其余元素 value-init（默认构造） |
| 6 架构 MIR 路径 | ✅ | varargs 全打通，扩展矩阵全 PASS（109a3ff） |
| C 覆盖达成 | ✅ | C99/C11/C23 全部实现：__VA_OPT__/__has_c_attribute（c60874d）、C23 constexpr 函数求值（753df8a）——**C 90~23 目标达成** |
| 验收门禁 | ✅ | `test/verify-all.sh` + `make check-all`（c940c34） |
| 全面验收基线 | ✅ | verify-all 6/6 + 自举产物 + 6 架构 qemu 矩阵（b5dcc8d，docs/acceptance.md） |
| P2 spill slot 复用 | 📋 方案定稿 | rega/spill 长期专项，spill slot 生命周期复用方案（c84555f，p2-spill-slot-reuse.md）；实施中 |

### m++ 已知限制（截至 2026-08-02）

- **模板**：显式模板实参 `conv<int>(x)` 中 `<` 偶尔被当比较符（有歧义场景）、模板与重载共存、
  非类型模板参数；类模板成员函数体急切实例化（C++ 语义为按需惰性）、
  类模板作函数返回值触发既有聚合返回拷贝限制；变参模板无 C++17 折叠表达式外的
  递归自我转发选择：`sum_all(1,2,3,4)` 调用点在**重载选择**阶段未把变参重载
  `(T, Ts...)` 排为可吸收多于定长重载 `(T)` 实参的候选取向，且嵌套实例化时
  pack 参数(`rest_0`)值绑定错位——两层都属「跨层状态/语义一致性」深根，
  **登记 mcc 一致性专项**；无包嵌套、
  无类成员变参模板。`sizeof...(Ts)` 多元素计数与 `count<>()` 空包已修（2eaa662a）。
- **i386 i64 stack-param 缺陷**（独立于常量槽位，登记 mcc 一致性专项）：i386 上
  `long long` 形参（cdecl 栈传参，位于 `8(%ebp)`）读成 `-1(%ebp)`/`3(%ebp)`——根因是
  `mabi_selpar` 在 lowering 期把 `dst->slot`（此时为 `-1` 未分配 sentinel）直接写进 LOAD
  目标 `lov->slot`，regalloc 之后不会再给该 param 一个真实 slot（常量化路径相同缺陷已修，
  见下）。`i386 test/i386/i64const.c` 刻意规避该路径，仅覆盖常量槽位回归。
- **继承**：虚继承。纯虚 `= 0` 已支持（纯虚成员/析构声明解析 + vtable 槽位留 0，派生覆写正常分派）；抽象类（含未覆写纯虚）实例化报错已支持（对象声明/`new T`/`new T[]`）；类外析构定义 `B::~B(){}` 已支持（纯虚析构完整对象生命周期可用）；多态 `delete` 基类指针已支持（虚析构经 vtable 分派跑派生析构 `~D` 再 `~B`，非虚析构仍静态决议，`delete[]` 数组仍走静态逐元素析构）。
- **auto/decltype**：仅 `auto x = expr`（局部/全局）与 C++14 `auto f()` 返回类型推导；未做 `auto&` 引用折叠、decltype 独立推导（160e2a2 落地范围）。
- **lambda**：值捕获与引用捕获均已支持——显式 `[&x]` / 默认 `[&]` 引用捕获（能读到活动变量更新）、默认按值 `[=]`、混捕 `[=,&y]`/`[&,z]`/`[x,&y]`、init-capture `[n=expr]`、泛型 lambda（c14c7a2 起支持引用/init 捕获）；跨函数传递捕获仍为限制。
- **constexpr**：整型常量折叠 + static_assert + 数组维度编译期求值 `int a[f()]` + constexpr 变量/函数初值 + static/constexpr 成员 init 已支持（3ac233b 等）。非类型模板实参取 **函数调用** `arrsize<sq(3)>()` 已支持（9c40acec：NTTP 实参可为 constexpr 函数调用 `arrsize<sq(3)>/nine()/sq(sq(2))/sq(2)+1`、constexpr 变量、字面量）。**constexpr 成员函数调用折叠** `constexpr S s={..}; constexpr int r=s.m(2)` 已支持（ca410f20：成员 isconstexpr 标定 + 两阶段 body 注册进常量求值器 + `&obj` this 首参跳过 + 对象成员按名绑进回放 scope）。（变参递归 self-forwarding gap 见模板行/一致性专项。）
- **其它**：函数指针声明参数里的类名未识别（独立问题）。
- **MIR 路径遗留**（非 m++ 专属）：自举 mcc 编译「聚合参数+varargs+栈传参」组合在
  declspecs 写 NULL（Bug B 待调）；atomic_concurrent/thread_local 多架构 TLS 既有问题。

### m++ 缺陷队列（截至 2026-08-03，mcc-team-0599；编号迁移为组件前缀+hex）
> **编号体系说明**：2026-08-03 起缺陷编号为「组件/阶段前缀 + 两位 hex」——`cpp-`（C++ 前端）、`c-`（C 前端）、`mir-`（MIR）、`x86-`（x86_64 后端）。旧字母保留对照。

| 编号（新） | 旧 | 缺陷 | 状态 |
|---|---|---|---|
| cpp-01 | B | 自由函数重载被拒 | ✅ closed（83db5ff） |
| cpp-02 | C | 继承析构链缺失 | ✅ closed（c19a351） |
| cpp-03 | D | static void 方法误判构造 | ✅ closed（16f1948） |
| cpp-04 | E | ns 四项限制 | ✅ closed（6f3d734） |
| cpp-05 | G | 泛型 lambda 捕获 | ✅ closed（3f0ed41） |
| cpp-06 | H | 限定+虚调用 | ✅ closed（a096b52） |
| cpp-07 | K | concept 递归深度 | ✅ closed（2755fe3） |
| cpp-08 | M | 未命名参数 ctor | ✅ closed（4d93a66） |
| cpp-09 | N | 数组 new stride | ✅ closed（754b437） |
| cpp-0a | Q | `delete nullptr`/`delete[] nullptr` 段错误 | ✅ closed（f8f0044） |
| cpp-0b | R | concept 形参名 ≠ `T` 误判 undeclared | ✅ closed（93ab4b4） |
| cpp-0c | S | lambda 按值捕获类对象不调拷贝构造 | ✅ closed（f8f0044 混入） |
| cpp-0d | T | 嵌套 lambda 无法再捕获外层变量 | ✅ closed（f8f0044 混入） |
| cpp-0e | Z/U | size-0 空类按值传参/返回崩溃（P0） | ✅ closed（三处：2be27a7 LIR 路径 + e4a885c MIR 后端空聚合 ABI 归一 + 00ed62b MIR 后端参数 DCE/regalloc；empty_class_value.cc 双路径完整闭环） |
| cpp-0f | Y | `delete (T*)expr` 解析失败 | ✅ closed（9e43494，delete/delete[] operand 改 castexpr + new_delete_cast.cc） |
| c-00 | W | `u8"..."` 字面量元素类型应为 `char`（C11 §6.4.5p6） | ✅ closed（604be9e，expr_literal.c case '8' 改 &typechar + u8_string.c 类型守卫） |
| c-01 | X | inline 定义 + extern 声明未发外部定义（C99 §6.7.4p7） | ✅ closed（e9fae35，decl.c 延迟发射 + extern promote + inline.c 双向断言，自举通过） |
| mir-00 | F | fold shl/sar(x,0) 优化缺口 | ✅ closed（647a05b 夹带 + 实证复验） |
| mir-01 | V | MIR msimp 有符号 div/rem 误削减 | ✅ closed（93ab4b4 夹带 + Test 3b/3c/3d + 4c24bfe） |
| mir-02 | J | slotmerge 自举破坏（长期禁用 97c8541；二期见 worker-slot2） | 🚫 长期禁用 |
| mir-03 | I | slotmerge 崩溃（并入 J） | 🚫 禁用 |
| x86-i64slot | — | i386 i64 常量槽位 lo/hi 约定不一致（物化端不 base 化 `g_slot_base` + `-1` sentinel 直用致 `-1(%ebp)`/`3(%ebp)`，`(1LL<<40)>>32` 读垃圾）→ rr_i64 | ✅ closed（6008c405；i386_memit 引入 i64_base/i64_dst_base 统一 base 约定 + 预留 scratch 半对；i386 test/i386/i64const.c 回归 + rt_matrix i386 8/8） |
| x86-i64param | — | i386 i64 栈传参读 `-1(%ebp)`/`3(%ebp)`（mabi_selpar/selcall/selret/vaarg 用 lowering 期未分配 slot 直写；libc 侧 L".." 真实宽字面量需编译含 i64 形参代码） | ✅ closed（cfd39be9 + #16 系列；selpar 单 MMOP_LOAD MT_I64 / selcall 单 MMOP_STORE / ret const 保 imm / vaarg 单 load；rt_matrix rr_i64param 全架构 PASS） |
| a64-jccfall | #19 | aarch64 JCC 终止子缺 s2 fallthrough 显式跳转（块按 fm->link 发射非 CFG 序，条件假时落任意块，多 if 的 main 无限循环 exit=124）→ rr_i64param 挂 | ✅ closed（fda34544；emit_block JCC 后无条件 `b .L<fn>.bb<s2>`，对齐 x86_64；test/aarch64/jccfall.c + gate；rt_matrix 解除 xfail，6 架构 9 程序全 PASS） |
| x86-movzx-lea | #22a | i386 `i8/i16→i32` MOVZX 落入 MMOP_LEA 发 `leal 0,%eax` 清零，`(unsigned char)int` 返回 0 非 0x78 | ✅ closed（7b907823；emit_ins MOVZX 显式 movzbl/movzwl + scratch_to_dst） |
| la64-fpconst | — | loongarch64 FP 常量物化成整数 0（`li.d $t0,0x0; movgr2fr.w` 从截断 bit pattern；LoongArch 无 64 位 FP 立即数）→ rr_fp | ✅ closed（090fa569；照抄 x86_64 fp_pool/.LlcN：FP 常量 stash .rodata + `pcalau12i/addi.d` 载址 + `fld.s/.d`；loongarch64 test/fp_const.c gate；rt_matrix loongarch64 8/8） |
| crossarch-matrix | — | 跨架构 QEMU runtime 矩阵 xfail 收官 | ✅ 全 6 架构 × 9 程序全 PASS、零 xfail（i386 6008c405/cfd39be9 + loongarch 090fa569 + aarch64 fda34544 后 progs_xfail 清空） |
| wstring-dedup | — | 宽字面量同 TU 不同内容被误合并（`stringdecl` 用元素数 `size` 作 mapkey 长度，wchar 只比首元素字节 → L"abc"/L"abd" 都开 'a' 而合并成一个 .Lstring） | ✅ closed（6149ecde；`stringdecl` 改 `size * expr->type->base->size` 按字节比较；test/c99/wide_string_dedup.c 运行时回归） |
| cpp-10 | — | 局部类（函数体内 `struct`）+ `new` 段错误：ctor 体即时代码生成污染全局 `curfunc` + 局部类 `t->scope` 未设（野指针） | ✅ closed（三处：mktype 初始化 `t->scope=NULL` + tagspec 普通 struct 设 `t->scope=s` + cpp_parse_method_body 恢复 `curfunc`；local_class_new.cc 回归） |
| x86-00 | va_list | MIR 后端 va_list 溢出 | ✅ closed（222a28d） |
| cpp-00 | A | size-0 类值传参（历史名，已被 cpp-0e 替代） | 🚫 废弃 |

> 状态规则：缺陷状态只标 open/pending；修复提交 push 后由 worker-doc 周期 pull 补记 closed + 哈希。C++ 覆盖状态见上表（C++98~23 收官 ✅，2026-08-02）。
> MIR 单路径门禁：`make check-c-mir`（`test/mir_matrix.sh`），c99/c11/c23 用例全部经唯一 MIR 路径编译+运行，退出码均为 0（MIR 为唯一 asm 生产者，LIR 双路径对比已随 LIR 桥接层移除）。

## 8. Progressive Cleanup Notes

The user has asked for **progressive** removal of `cproc`/`qbe` naming
artifacts and structural integration of the two source trees. Status:

**Done in irgen-refactor (irgen/ split round)**:
- `cproc_op_to_qbe()` -> `fe_to_ir_op()` (in `src/c/irgen/emit.c`)
- Comment cleanup: most "cproc does X" -> "the frontend does X"
- File split removed monolithic `irgen.c` (no cproc/qbe filename in `src/c/irgen/`)

**Done in Phase 1b (control flow + call round)**:
- Pass 2 emit pattern switched from BACKWARD `emit()` to FORWARD
  `*curi++` writes - eliminates instruction-order inversion bug.
- `err()` redefined in `src/ir/ir_util.c` - was originally in
  `parse.c` (deleted in irgen-refactor). Without the definition the linker
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
  - `irgen/` -> `src/c/irgen/`
  - `backend/{ir,opt,abi,emit}/` -> `src/{ir,opt,abi,emit}/`
    (removed `backend/` intermediate layer)
  - `target/` -> `src/target/` (with amd64/arm64/rv64 subdirs)
- **Makefile**: `SRC_DIRS := src` (single recursive root)
- **Symbol renames** (internal API, no public ABI impact):
  - `qbetype()` -> `irtype()` (in `src/c/irgen/value.c`)
  - `b->qbe` -> `b->ir` (struct block field, IR Blk pointer)
  - `run_qbe_passes()` -> `run_passes()` (in `src/c/irgen/emit.c`)
  - `ir_to_qbe_op()` -> `fe_to_ir_op()` (in `src/c/irgen/emit.c`)
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
- **`src/c/lex/pp.c`**: implemented previously-missing preprocessor
  directives `#if`/`#ifdef`/`#ifndef`/`#elif`/`#else`/`#endif`
  (conditional compilation stack + skip logic), `#include` (with -I
  path search + quote/angle-bracket distinction), `#if` constant
  expression evaluator (with `defined`, macro expansion, recursive
  descent). Public API: `ppdefine()`/`ppundef()`/`ppincludepath()`/
  `ppdumpdeps()`.
- **`-target` triplet mapping**: public target identifiers are
  `x86_64`, `aarch64`, `riscv64`, `i386`, `loongarch64`, and `arm`.
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
- ✅ `-O` level control (done: O0/O1/O2/O3/s, pass gating in run_passes).
- ⚠️ `-W`/`-w` warning system (flag parsing + Fn infrastructure done;
  actual warn() function and per-pass warning calls not yet implemented).
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
| `reference/cxx-frontend/` | HEAD | `master` | m++ C++23 frontend reference (lex/parse/sema/AST) |
| `reference/aburi/` | v0.1.1 | `master` | m++ C++ frontend full-pipeline reference |

When investigating an IR pass behavior, the canonical source is
`reference/qbe/<file>.c`. The corresponding mcc copy lives in
`mcc/src/opt/<file>.c` (or `mcc/src/ir/`, `mcc/src/emit/`).
cproc frontend files mirror 1:1 to `mcc/src/{lex,parse,sema,util}/`.
