# B.5 目录迁移方案（C 前端 → `src/c/`）

> 调研报告（只调研，未实际迁移）。依据：`cosmic-forging-einstein-Cjwtv5Wd.md` 计划 A 节目标架构。
> 分支：`worktree-mxx-work`。日期：2026-08-02。

## 1. 背景与目标

计划 A 节要求 mcc 重构为「共享后端 + 多语言前端」gcc 风格结构。C 前端目录
从 `src/` 顶层平铺迁入 `src/c/` 子目录：

```
src/lex   → src/c/lex
src/parse → src/c/parse
src/sema  → src/c/sema
src/irgen → src/c/irgen
```

目标结构（计划 A 节）：
```
src/
├── driver/   # main.c(mcc) mpp_main.c(m++) common.c ...（不动）
├── c/        # C 前端：lex/parse/sema/irgen 迁入
├── cpp/      # C++ 前端（lex/parse，已存在）
├── mir/ lir/ ir/ opt/ abi/ emit/ target/ util/   # 共享后端（不动）
```

## 2. 当前结构 vs 目标

| 当前 | 目标 | 文件数 |
|---|---|---|
| `src/lex/` | `src/c/lex/` | 5 |
| `src/parse/` | `src/c/parse/` | 17 |
| `src/sema/` | `src/c/sema/` | 6 |
| `src/irgen/` | `src/c/irgen/` | 12 |
| **合计** | | **40** |

不迁移：`src/driver/`（11）、`src/cpp/`（2，C++ 前端）、`src/util/`（2，共享层，见 §7 决策点）。

## 3. 文件清单

### `src/c/lex/`（5）
`pp.c` `pp_expr.c` `pp_internal.h` `scan.c` `token.c`

### `src/c/parse/`（17）
`attr.c` `decl.c` `declarator.c` `decl_internal.h` `expr.c` `expr_binary.c`
`expr_generic.c` `expr_internal.h` `expr_literal.c` `expr_postfix.c`
`expr_primary.c` `expr_stmt_expr.c` `expr_unary.c` `specs.c` `stmt.c`
`struct_decl.c` `tree.c`

### `src/c/sema/`（6）
`eval.c` `init.c` `map.c` `scope.c` `targ.c` `type.c`

### `src/c/irgen/`（12）
`branch.c` `convert.c` `emit.c` `emittype.c` `expr.c` `func.c` `funcmem.c`
`func_to_mir.c` `inst.c` `irgen.h` `switch.c` `value.c`

## 4. #include 调整点

审计结果：**仅 2 处跨目录相对 include 需调整**，其余全部自动跟随。

### 需调整（2 处，均在 cpp 前端，因 parse 迁移后相对路径变化）
```
src/cpp/parse/cpp_parse.c:23:#include "../../parse/decl_internal.h"
src/cpp/parse/cpp_parse.c:24:#include "../../parse/expr_internal.h"
```
迁移后 `src/cpp/parse/cpp_parse.c` → `src/c/parse/*.h` 的相对路径：
```
#include "../../c/parse/decl_internal.h"
#include "../../c/parse/expr_internal.h"
```

### 无需调整（自动跟随）
- **同目录相对 include**：`irgen.h`、`expr_internal.h`、`decl_internal.h`、
  `pp_internal.h` 均被**同目录**文件引用（文件随目录一起移动，相对路径不变）。
  已逐项核对：`irgen.h`（irgen 内 11 处）、`expr_internal.h`/`decl_internal.h`
  （parse 内全部）、`pp_internal.h`（lex 内 2 处）均无跨目录引用。
- **include/ 头文件**：`mcc.h` `util.h` `tokens.h` `ir.h` `mir.h` 等通过
  `-Iinclude` 解析（`include/` 目录不动），引用不变。
- **带前缀的 include/ 头**：`cpp/cpp_tokens.h`、`mt/msys.h`、`mt/target.h`
  相对 `include/`，不受影响。

### 确认无其他跨目录引用
- `src/` 全部 `#include "..."` 已 grep 全量审计，唯一跨目录的是上述 2 处
  `../../parse/*.h`（`src/cpp/parse/` 内）。
- 无 `#include "../..."` 形式的其它引用。

## 5. Makefile 改动

### 必需（1 行核心 + 1 行可选）
```
第 63 行  FE_DIRS := src/driver src/cpp src/c/lex src/c/parse src/c/sema src/c/irgen
```
或简化为（`src/c` 递归收集，当前 src/c 下仅有 4 个子目录）：
```
FE_DIRS := src/driver src/cpp src/c
```
- `FE_SRCS := $(shell find $(FE_DIRS) ...)`（第 68 行）自动收集，无需改。
- `FE_OBJS`（第 70 行）`patsubst %.c,build/%.o` 自动映射
  `src/c/lex/pp.c → build/src/c/lex/pp.o`；`build/%.o` 规则（第 109 行）
  `mkdir -p $(dir $@)` 自动建目录，无需改。
- `BE_DIRS`（第 64 行）不变。

### 注释同步（非编译，建议）
- 第 12 行目录注释 `src/irgen/` → `src/c/irgen/`。
- `ARCHITECTURE.md` 第 17/36/163-167/180-190/217/230/314-364 行引用
  `src/{lex,parse,sema,irgen}` 的路径，需同步更新（文档维护，不阻塞编译）。

## 6. 其他引用（非阻塞）

- **自举 `check-sysroot-static`**（Makefile 第 238-250 行）：`cp -a .` 复制
  整个项目目录，源码路径变化不影响；`-Iinclude` 编译，源码相对 include
  保持一致。无需改。
- **外部项目**：全 worktree 搜索 `mcc/src/lex|parse|sema|irgen`，meuos-libc/
  meuos-toolchain 等其他项目**零引用**。
- **测试脚本**：`test/*.sh` 无硬编码 FE 源码路径（仅 ARCHITECTURE.md 与
  test/community/chibicc/REPORT.md 有文档引用，非编译）。
- **.gitignore**：仅 `m++`，不受影响。

## 7. 决策点：`src/util/` 归属

计划 A 节写 `src/c/ # C 前端（lex/parse/sema/irgen/util 迁入）`，但当前
`src/util/`（`utf.c` `util.c`）在 **BE_DIRS**（libmcc.a 共享层，`util.h` 被
FE 的 lex/parse/sema/irgen/driver/cpp 共 30 处引用，BE 侧不直接引用）。

两个选项：
- **A（建议）**：util 保持共享层（`src/util/` 不动）。理由：util 是
  FE/BE 共享工具（对应计划 `src/c-family/` 共享层精神），迁移徒增
  FE_DIRS/BE_DIRS 边界混淆，且 cpp 前端也引用 `util.h`。本期仅迁
  lex/parse/sema/irgen 四目录，完全满足「C 前端归位」目标。
- **B（严格按计划字面）**：`src/util/` → `src/c/util/`，BE_DIRS 去掉
  `src/util`，FE_DIRS 含 `src/c` 自动收集。改动仅 Makefile 两行，但 util
  从共享层移入 C 前端，未来 C++ 前端共享时语义需重审。

## 8. git 历史影响

- 使用 `git mv src/lex src/c/lex`（及 parse/sema/irgen）保留历史；
  git 按内容检测 rename，commit 中显示 rename 而非 delete+add。
- 建议**每目录一个 commit**（4 个）或**迁移 + Makefile + 文档一个 commit**：
  1. `mcc: move C frontend lex/parse/sema/irgen under src/c (B.5)`
  2. `mcc: update Makefile FE_DIRS + cpp include paths for src/c (B.5)`
  3. `docs: b5-migration + ARCHITECTURE path updates (B.5)`
- 迁移后 `build/` 残留旧路径 `.o/.d` 属构建产物，`make clean` 即可。

## 9. 验证步骤

1. `make clean && make -j` → mcc/m++ 构建成功。
2. `make check`（hello）PASS。
3. `make check-c99`（MEUOS_SYSROOT 或 host 回退）全 PASS。
4. `make check-c11` 全 PASS。
5. `make check-cpp`（C++ 前端回归，验证 cpp_parse.c include 调整）PASS。
6. `make check-sysroot-static`（自举：mcc 编译 mcc）PASS。
7. `MCC_MIR_BACKEND=1` 抽样：8 变参 / c99 varargs（验证 MIR 后端路径）。
8. `git log --follow src/c/lex/pp.c` 确认历史保留。

## 10. 风险与执行时机

### 风险
| 风险 | 等级 | 缓解 |
|---|---|---|
| cpp 前端 2 处 `../../parse/*.h` 忘改 → cpp_parse.c 编译失败 | 低 | §4 已列清单；check-cpp 兜底 |
| 同目录 include 在移动后因工具链差异解析失败 | 低 | 编译器默认"当前文件目录优先"解析引号 include，已验证全部同目录 |
| 大 commit 影响 code review | 低 | 分目录 commit / git mv 显示 rename |
| worker-cpp-2 在途改动与迁移文件冲突 | **中** | 见下"执行时机" |
| `src/c` 未来混入 c-family/util 造成 FE_SRCS 意外收集 | 低 | 本期用显式 4 子目录 FE_DIRS 或接受 src/c 递归（当前仅 4 子目录） |

### 执行时机（关键结论）
**必须等 worker-cpp-2 提交后再执行。**

当前 worker-cpp-2 有 3 个在途改动落在待迁移目录：
```
src/parse/declarator.c   （待迁移 → src/c/parse/）
src/sema/init.c          （待迁移 → src/c/sema/）
src/sema/type.c          （待迁移 → src/c/sema/）
```
`git mv` 对未提交的在工作区改动会失败/丢失。迁移前须：
1. worker-cpp-2 提交移动语义相关改动（涉及 declarator.c/init.c/type.c）；
2. 确认工作区干净（仅余本次迁移）；
3. 一次性 `git mv` 四目录 + 改 Makefile/cpp include + 文档。

预计影响：4 个目录 40 文件移动 + 1 行 Makefile + 2 行 cpp include + 文档同步，
约 1 个 commit 组（2-3 个 commit）。不阻塞其他 worker（纯结构迁移，
`git mv` 保留历史，编译行为零变化——仅路径变化）。

## 11. 结论

迁移影响面**极小**：
- 40 文件 `git mv`（4 目录）
- **2 处** cpp include 路径调整（`../../parse/*` → `../../c/parse/*`）
- **1 行** Makefile FE_DIRS
- 文档同步（ARCHITECTURE.md 路径引用）

编译行为零变化（同目录 include 自动跟随、-Iinclude 头文件不受影响、
自举 `cp -a` 无需改、外部项目零引用）。唯一前置条件是 worker-cpp-2
提交其在 declarator.c/init.c/type.c 的在途改动，避免 git mv 冲突。
