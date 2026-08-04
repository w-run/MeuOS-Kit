# 大文件分层重构（优化输入 token / 提升缓存命中率）

> 状态：🔄 开放（2026-08-04 大喵 token 优化方向 + doc-pm 扫描各组件超大源文件）
> 关联 commit：无（纯重构，零行为改变）
> ⚠️ **适用分支**：清单文件基于 **`tmp/lead-doc-mir-baseline`（m++/MIR 主线）** 核实（main 为另一套源结构，不适用）；重构动作须在 lead-doc-mir-baseline 系 worktree 上完成。

## 动机

- 超大源文件是 **缓存命中率主要障碍**：worker 读取大文件会把大量不相关的函数/符号拉进上下文，降低命中率、抬高 token 成本；
- 重构为小文件（目标 ~800–1200 行），提升后续所有组件工作的 token 效率与并行度；
- 大喵要求方向：代码结构重构、大文件分层拆分、允许多层级目录、减少单个大文件体积。

## 纯重构定位（零行为改变）

- 按**模块边界**分层拆分（lex / parse / sema / irgen / layout / reloc / dynamic / elfout 等）；
- **不改变任何逻辑**；拆分后行为与拆分前完全一致（可编译产物二进制可比对）；
- 风险控制 = 每个拆分落地后立即跑门禁防回归。

## 待拆大文件清单（行数已在 lead-doc-mir-baseline 核实）

### mcc
| 文件 | 行数 | 建议拆分 |
|------|-----:|----------|
| `src/cpp/parse/cpp_parse.c` | **10569** | lex / parse / sema / irgen（C++ 前端全塞一文件，**最高收益**） |
| `src/c/lex/pp.c` | 1943 | 预处理各阶段拆分 |
| `src/target/i386/i386_emit.c` | 1966 | 发射逻辑分模块 |
| `src/target/arm/arm_emit.c` | 1578 | 发射逻辑分模块 |
| `src/target/x86_64/x86_64_memit.c` | 1400 | 发射逻辑分模块 |
| `src/mir/passes.c` | 1222 | MIR pass 分文件 |

### meuos-toolchain
| 文件 | 行数 | 建议拆分 |
|------|-----:|----------|
| `src/ld/link.c` | **4761** | layout / reloc / dynamic / elfout（链接器全塞一文件，**最高收益**） |
| `src/libdisasm/x86_64.c` | 2553 | 反汇编分模块 |
| `src/as/assemble.c` | 2406 | 汇编驱动分模块 |
| `src/target/aarch64/encode.c` | 1746 | 指令编码分模块 |
| `src/target/i386/encode.c` | 1853 | 指令编码分模块 |
| `src/target/arm/encode.c` | 1532 | 指令编码分模块 |
| `src/target/riscv64/encode.c` | 1526 | 指令编码分模块 |
| `src/target/loongarch64/encode.c` | 1081 | 指令编码分模块 |
| `src/target/x86_64/encode.c` | 1389 | 指令编码分模块 |

### meuos-libc（较次要）
| 文件 | 行数 | 说明 |
|------|-----:|------|
| `src/stdio/fp_fmt.c` | 685 | 浮点格式化，可分模块 |
| `src/netdb/gethost.c` | 602 | 可拆分 |
| `src/dlfcn.c` | 460 | 可拆分 |

## 优先级

> `cpp_parse.c`（10569）与 `link.c`（4761）收益最高，**先拆这两个**。

1. `cpp_parse.c`（mcc）→ lex/parse/sema/irgen；
2. `link.c`（mt/ld）→ layout/reloc/dynamic/elfout；
3. 其余按上表逐一下行。

## 验收

- 每个拆分落地后：`make -C projects/mcc check` 或 `make -C projects/meuos-toolchain check`（对应组件）**全 PASS**；`verify-all.sh`（mcc 含 check-mir）全 PASS；
- 单文件行数降到 **~1200 以下**；
- 行为与拆分前**完全一致**（编译产物/门禁行为可比对，零回归）。

## 范围约束

- 纯重构由 exec-mcc / exec-toolchain 按模块拆分实施；doc-pm 只登记与追踪；
- 拆分前先确认在 lead-doc-mir-baseline 系 worktree 上定位到同名源文件（main 结构不同）；
- 落地后经验沉淀到 `.agents/knowledge/`。
