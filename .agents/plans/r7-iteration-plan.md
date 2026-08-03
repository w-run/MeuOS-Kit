# mcc-team-r7 迭代计划（r7-iteration-plan.md）

> 状态：规划文档（planner，2026-08-03）。**只调研不改代码**。
> 基线：HEAD `3446850`（worktree-mxx-work，工作树干净；origin 同步）。
> 工作树：`/workspace/MeuOS-Kit/.agents/worktrees/mxx-work`。
> 目标：把 C++/C 标准覆盖缺口 + 已知缺陷 + 自主优化机会拆成**细颗粒度、可并行、验收可断言**的任务，供后续 executor worker 批量派发。

---

## 0. 基线快照（HEAD 3446850 实测）

| 项 | 数值/状态 | 来源 |
|:---|:---|:---|
| `verify-all.sh`（默认 + `MCC_MIR_BACKEND=1/0` 双路径） | **19/19 PASS** | test/verify-all.sh |
| `check-chibicc` | **PASS=16 / RUNFAIL=0 / COMPILEFAIL=25**（共 41） | test/community/chibicc/results.log（已排除出 verify-all.sh） |
| `check-pic-verify` | **FAIL**（riscv64/i386 GOT 已知缺口） | pic_verify.sh |
| `check-cpp`（lex/virtual/func/neg） | **PASS** | Makefile |
| `test/cpp/` 规模 | **165 文件**（含 10 个 `pending/` 回归标记） | ls test/cpp/ |
| `test/c{99,11,23}/` | 共 **~75 测试** | ls test/c*/ |
| `src/cpp/parse/cpp_parse.c` 行数 | **8146 行**（单文件大热点） | wc -l |
| `src/c/parse/*.c` | 17 文件 / 6587 行 | wc -l |
| `src/mir/*.c` | 10 文件 / 4645 行 | wc -l |
| 最近 24h 提交主题 | r5 团队 + 6 临时分支归并（eve-p4step1/diana-errcode2/hazel-aafill/hazel-bench/bella-la64fill/chloe-arm + bella-perf） | git log -20 |

**已知闭环（本次无需重排）**：
- C++98~20 主路线图全部 ✅（auto/lambda/constexpr/变参/move/concepts/if constexpr/CTAD/结构化绑定/范围for/consteval/多语句constexpr/deducing this/多维operator[]/if consteval）
- C++23 四缺口 + 三缺口 + C23 三缺口 全部已合入（参考 worker-deployment.md §4 与 memory：`project_cpp23_gaps.md`、`project_cpp_remaining_gaps.md`、`project_c23_gaps.md`）
- chibicc B 类真 bug 已合入（`hazel a932c60`）
- errcode 全覆盖 E0005-E0012 已合入（`diana e72cfea` + 7 commit）
- mt/as imm64 截断修复已合入（`alice ad52f9b`）
- MIR 机器层性能优化已合入（`bella c432b87`）

**当前真实阻塞清单**（来自 cpp23-gaps.md §2 / cpp20-gaps.md §2.6 / cpp20-gaps.md §4 / c23-review.md / chibicc REPORT.md §5.B）—— **本计划的核心工作面**。

---

## 1. 调研结论总览

### 1.1 C++ 缺口（基线 cpp23-gaps.md §2 + cpp20-gaps.md §2.6 已确认）

| # | 缺口 | 文件:行号 | 难度 | 优先级 | 类别 |
|:--:|:-----|:----------|:----:|:------:|:----:|
| **K** | **concept 递归深度 16 上限** | `src/cpp/parse/cpp_parse.c:4729` `MAX_CONSTRAINT_DEPTH=16` | 低 | **P0** | 缺陷 |
| **D1** | **类类型运算符 `const T&` 形参** | `cpp_try_operator_call`（`cpp_parse.c:1274-1339`，worker-deployment.md §4 D1 已确认） | 中高 | **P0** | 缺陷 |
| **D4** | **非空类按值返回错乱** | 返回路径（空类已修 `2be27a7`，非空未推广） | 中高 | **P0** | 缺陷 |
| **REQ** | **requires 表达式四类需求** | `cpp_parse.c` 概念体缓冲 + requires-clause 缓冲器（worker-req4 在途未合入，c9ca880 wip 分支） | 中 | **P0** | 覆盖 |
| **NTTP-FIX** | 依赖类型 NTTP 残留 | `cpp_parse.c`（grace `ef89d22` 已实现，残留边界：NTTP+类模板+显式实参混合） | 低 | **P0** | 边界 |
| **CT-REQ** | 类模板 + requires-clause 顺序 bug | `cpp_parse.c:3759 is_class` 早于 `:3822 requires` 消费 | 低 | **P0** | 缺陷 |
| **D2-LAZY** | 急切实例化未使用成员函数 | `flush_pending_methods`（alice `d28c744` 已修；cpp23-gaps.md §2 提及"标准正确行为是惰性"，尚有边界残留） | 低 | P1 | 缺陷 |
| **D3-PP** | `#elifdef` 前组求值 | `src/c/lex/pp.c` 跳过组内 `#elifdef` 求值分支（worker-pp4 在修） | 低 | P1 | 缺陷 |
| **NOEX** | `noexcept` 限定符 | `cpp_parse.c` 缺 noexcept 识别 | 中 | P1 | 基线 |
| **EXTC** | `extern "C"` 连接说明 | `cpp_parse.c` 缺 extern 字符串字面量识别 | 中 | P1 | 基线 |
| **LAMBDA-CAP** | init-capture / 引用捕获 lambda | `cpp_lambda_expr` primaryexpr 入口 | 中 | P1 | 基线 |
| **CINIT** | `constinit` 关键字 | `include/cpp/cpp_tokens.h`（无 CPP_TCONSTINIT）+ `cpp_scan.c` | 低 | P1 | 覆盖 |
| **C8T** | `char8_t` 类型关键字 | `cpp_tokens.h`（无 CPP_TCHAR8）+ 字面量类型推导 | 中 | P1 | 覆盖 |
| **EXPB** | `explicit(bool)` 条件 | `src/c/parse/specs.c` 缺 explicit(expr) 处理 | 低 | P1 | 覆盖 |
| **UENUM** | `using enum E;` | `cpp_scan.c` 缺 using-enum 识别 | 低 | P1 | 覆盖 |
| **LTPL** | lambda 模板参数 `[]<typename T>` | `cpp_lambda_expr` `[` 入口只解析 `[cap](params)` | 中 | P1 | 覆盖 |
| **SSHIP-DFT** | 类类型三向比较 `= default` | `cpp_op_mangle` TSPACESHIP 无 default 分支 | 中 | P1 | 覆盖 |
| **FOLD** | fold 表达式 `(... + args)` | pending/fold_expr.cc 标记 | 中高 | P2 | 覆盖 |
| **DCLT** | `decltype` 关键字 | pending/decltype.cc 标记 + `cpp_tokens.h`（无 TDECLTYPE） | 中 | P2 | 基线 |
| **ENUMC** | `enum class` 作用域枚举 | pending/enum_class.cc 标记 | 中 | P2 | 覆盖 |
| **DFTMP** | 模板默认实参 `template<T=int>` | pending/default_tmpl_arg.cc 标记 | 低 | P2 | 覆盖 |
| **DSGN** | C++20 指定初始化器 `T x{.a=}` | pending/designated_init.cc 标记（C 形式已通） | 中 | P2 | 覆盖 |
| **UINI** | uniform_init `T x{...}` 完整支持 | pending/uniform_init.cc（部分支持 alice 已修聚合初始化） | 中 | P2 | 覆盖 |
| **INLNS** | `inline namespace` | cpp23-gaps.md §2 标记 | 低 | P2 | 覆盖 |
| **ATTR** | `[[deprecated]]` 等使用点警告 | `src/c/parse/attr.c` 仅 nodiscard 部分（`da7a107`） | 中 | P2 | 优化 |
| **UDL** | 用户自定义字面量 `operator""` | `cpp_op_mangle` 无 UDL 编码 | 中 | P2 | 覆盖 |

### 1.2 C 缺口（基线 chibicc REPORT.md §5.B + c23-review.md 闭环后）

| # | 缺口 | 文件:行号 | 难度 | 优先级 | 类别 |
|:--:|:-----|:----------|:----:|:------:|:----:|
| **C-LIT-F** | `100f` 浮点后缀（C23） | `src/c/lex/scan.c:number()` 后缀表 | 低 | P1 | 缺陷 |
| **C-LIT-U** | UINT64_MAX 字面量类型回退（C 6.4.4.1） | `src/c/lex/pp_expr.c` / `scan.c` 整型回退 | 中 | P1 | 缺陷 |
| **C-MACRO** | 宏重定义相同 token 序列允许（C 6.10.3p2） | `src/c/lex/pp.c` macro redefine 处理 | 低 | P1 | 缺陷 |
| **C-ATOMV** | `_Atomic int*` → `void*` 限定对象指针转换 | `src/c/parse/type.c` `typecompatible` | 中 | P1 | 缺陷 |
| **C-LINE** | `__LINE__` 计算偏差 1 | `src/c/lex/pp.c` 行号递增点 | 低 | P1 | 缺陷 |
| **C-UCID** | Unicode 标识符 `$`/UCN | `src/c/lex/scan.c` 标识符识别 | 中 | P2 | 覆盖 |
| **C-VAEND** | `__builtin_va_end` 宏展开时类型检查过早 | `src/c/sema/eval.c` 或 `src/irgen/expr.c` 内建检查 | 中 | P1 | 缺陷 |
| **C-LIBC-COMPAT** | libc 头 vsprintf/memcpy 重声明不冲突 | `meuos-libc` 头（与 mcc 跨仓库协调） | 中 | P2 | 互操作 |

### 1.3 已知缺陷汇总（带字母编号，便于并行派发时锁定）

| 编号 | 简述 | 状态 | 文件定位 |
|:----:|:-----|:----:|:---------|
| **D1** | 类类型运算符 `const T&` 形参失败 | **待修** | cpp_parse.c:1274-1339 |
| **D2** | 急切实例化未使用成员函数 | 部分修（d28c744），边界残留 | cpp_parse.c flush_pending_methods |
| **D3** | `#elifdef` 前组求值误报 | worker-pp4 在修 | src/c/lex/pp.c |
| **D4** | 非空类按值返回错乱 | **待修**（空类已修 2be27a7） | 返回路径推广 |
| **H** | `c.A::f()` 限定调用绕过虚表 | **方案就绪未实施**（defect-h-fix.md §修复方案） | expr_postfix.c:493 |
| **J** | slotmerge 自举失败 | 已禁用（97c8541） | src/opt/slotmerge.c |
| **K** | concept 递归深度 16 上限 | **待修** | cpp_parse.c:4729 MAX_CONSTRAINT_DEPTH=16 |
| **M** | 未命名参数 ctor 编译段错误 | **待修**（test/cpp/unnamed_param_boundary.cc 是 regression marker） | cpp_parse.c |
| **N** | 数组 new 元素 stride | **已修**（754b437） | — |
| **U** | size-0 类按值传参 + 按值返回段错误 | 已修空类按值（2be27a7）；size-0 边界见 pending/value_param_member_call.cc | 已部分修 |
| **V** | MIR 有符号 div/rem | **已闭环**（93ab4b4 + 4c24bfe） | — |

### 1.4 自主优化方向（基线 bench-report.md §4）

性能（MIR-native vs GCC 差距 13-32x）：

| # | 优化 | 影响 | 难度 | 区域 |
|:--:|:-----|:-----|:----:|:-----|
| **OPT-A** | **参数寄存器 hint**（x86_64 rdi/rsi/... + riscv64 a0-a7） | 最大杠杆（消除每参数内存往返） | 中 | `src/mir/regalloc.c` + `func_to_mir.c` |
| **OPT-B** | **IndVarSimplify 循环优化 pass**（地址递增量 + strength reduction + 循环不变量提升） | intloop/fp_mat/strings/sortbench 循环体瘦身 3-5 倍 | 高 | `src/mir/passes.c`（新 pass） |
| **OPT-C** | postra 冗余 mov 消除（mov-coalescing + 复制传播） | 有效指令 -30-50% | 中 | `src/mir/regalloc.c` |
| **OPT-D** | regalloc 栈溢出优先级（活跃区间长/循环内变量优先寄存器） | 中 | 中 | `src/mir/regalloc.c` |
| **OPT-E** | div/rem-by-constant → multiply-shift | 小-中 | 中 | `src/mir/passes.c` |
| **OPT-F** | 小函数内联 pass（按函数大小阈值） | 小-中 | 中高 | `src/mir/passes.c` |
| **OPT-G** | 尾调用优化（TCO） | 小（防栈溢出） | 低 | `src/mir/passes.c` 或 `src/c/irgen/func.c` |

用户体验（自主特性方向 — 不做缝合怪）：

| # | 方向 | 难度 | 区域 |
|:--:|:-----|:----:|:-----|
| **UX-A** | `-ftime-report` 编译耗时分解报告 | 低 | `src/driver/` |
| **UX-B** | 错误诊断改进（模板回溯路径、修复建议） | 中 | `src/util/util.c` error_code 系 |
| **UX-C** | `-save-temps` / 产物控制 | 低 | `src/driver/` |
| **UX-D** | `--fdiagnostics-format=json` 扩展（`--error-json` 已有基础） | 低 | `src/util/util.c` |
| **UX-E** | DWARF 类/结构体类型 DIE（`debug_types` 简版） | 中 | `src/emit/dwarf.c` |
| **UX-F** | check-pic-verify 修复（riscv64/i386 GOT 序列）—— 解锁门禁 | 中 | `src/target/riscv64/*.c` + `src/target/i386/*.c` |

### 1.5 可并行性评估（按文件区域隔离）

**高隔离区（不同 worker 可完全独立）**：

| 区域 | 文件 | 可独立任务数 |
|:-----|:-----|:----:|
| `src/cpp/parse/cpp_parse.c`（分**行号段**） | 见 §3 任务表 | 8+ |
| `src/cpp/lex/cpp_scan.c` + `include/cpp/cpp_tokens.h` | 关键字/字面量 | 4（CINIT/C8T/DCLT/UENUM 等） |
| `src/c/parse/*.c` | C 缺陷 | 4+ |
| `src/c/lex/pp.c` | D3 / C-LINE / C-MACRO | 3 |
| `src/mir/*.c` | MIR 优化（OPT-A/B/C/D/E/F/G） | 5+（region 隔离） |
| `src/target/<arch>/` | 架构后端 | 每个 arch 独立 |
| `src/emit/dwarf.c` | DWARF 增强 | 1-2 |
| `src/driver/` | CLI/产物控制 | 3 |

**冲突风险**：
- `src/cpp/parse/cpp_parse.c`（8146 行单文件）—— **必须按行号段分给不同 worker**，所有 worker 提交前 `git status` 自查 + 文件级 `git add`
- `include/cpp.h`（全局声明）—— 任何新增全局 extern 必须先看是否有冲突
- `src/c/parse/stmt.c` 与 `src/cpp/parse/cpp_parse.c` —— TIF/TFOR 等语句入口冲突需协调（已由 alice/bella/grace 历史分工验证可行）

**已验证可并行的历史案例**：
- 6 分支归并（eve-p4step1/diana-errcode2/hazel-aafill/hazel-bench/bella-la64fill/chloe-arm + bella-perf）—— 7 worker 同工作树并行后归并无源码冲突
- 双 worker 竞争融合（chloe 4aaa11f + bella 72a04bd；grace/hazel 竞争 chibicc B 类）—— 团队纪律允许竞争落败方思路融合

---

## 2. 任务计划（按优先级 × 文件区域隔离）

> **粒度原则**：单 worker 任务 ≤1 文件主区；可独立验证；含可执行验收命令。
> **依赖图**：见每任务的 `依赖` 字段；同优先级内默认可并行。
> **验收命令约定**：
> - 默认模式：`./m++ --specs=host -o /tmp/t test/cpp/<新测试>.cc && /tmp/t`（exit 0）
> - 门禁模式：`make check-cpp-func check-cpp-neg` / `make check-c99 check-c11 check-c23` / `make check-all`
> - 双后端模式：`env MCC_MIR_BACKEND=1/0 make check-cpp-func check-cpp-neg`
> - 自主验证：`bash test/verify-all.sh`

### P0 任务（建议立即派发，8 个独立任务）

#### T01. **K 缺陷：concept 递归深度 16 上限**（缺陷修复）
- **区域**：`src/cpp/parse/cpp_parse.c:4729-4830`（`MAX_CONSTRAINT_DEPTH` 定义 + `eval_constraint` 深度检查）
- **修改**：将 `MAX_CONSTRAINT_DEPTH 16` 提升为 64 或 256（与 `g_cpp_cexpr_depth=64` 一致），或在深度达到时尝试「惰性求值 / 不报深度错误改为返回 false」避免误伤真实深度模板（如递归约束）
- **测试**：新增 `test/cpp/concepts_deep_recursion.cc`（10 层链式 requires + 20 层深度概念组合 + Concept<Concept<...>> 嵌套）
- **验收**：
  ```sh
  ./m++ --specs=host -o /tmp/t test/cpp/concepts_deep_recursion.cc && /tmp/t    # exit 0
  make check-cpp-func check-cpp-neg                                             # 全绿
  ```
- **依赖**：无

#### T02. **D1 缺陷：类类型运算符 `const T&` 形参**（缺陷修复）
- **区域**：`src/cpp/parse/cpp_parse.c:1274-1339`（`cpp_try_operator_call`）+ 调用点参数编码
- **修改**：4-way 级联查找（成员路径加 const-K/引用-R 编码回退）+ 引用实参 `&` 绑定（参考 expr_postfix.c:352-354 惯例）。worker-deployment.md §4 D1 已确认根因，alice 608f31c 已提交初步修复但边界（自由函数 + 模板 + 多重载）未覆盖
- **测试**：扩展 `test/cpp/operator_ref_const.cc`（自由函数/成员/模板形参/const T&/T&/T 三档）+ `test/cpp/operator_ref_const_boundary.cc`（含 `const A&&` 形参 + 模板推导）
- **验收**：
  ```sh
  ./m++ --specs=host -o /tmp/t test/cpp/operator_ref_const_boundary.cc && /tmp/t
  env MCC_MIR_BACKEND=1 make check-cpp-func check-cpp-neg
  env MCC_MIR_BACKEND=0 make check-cpp-func check-cpp-neg
  ```
- **依赖**：无（独立 cpp_parse.c 区段）

#### T03. **D4 缺陷：非空类按值返回错乱**（缺陷修复）
- **区域**：返回路径推广（参考空类已修 `2be27a7`）—— `src/c/irgen/func.c`（按值返回发射）+ `src/c/irgen/value.c`（按值赋值）
- **修改**：把空类 `size==0` 的 ABI 处理推广到「聚合按值返回」的通用路径（首成员隐藏指针/sret 选择 + 调用方帧布局）
- **测试**：`test/cpp/aggregate_return.cc` 已存在但只覆盖空类；新增 `test/cpp/aggregate_return_nonempty.cc`（`V make(){V v(7); return v;}` 各种聚合大小：8B/12B/16B/24B/40B）+ `test/cpp/return_pair_value.cc`
- **验收**：
  ```sh
  ./m++ --specs=host -o /tmp/t test/cpp/aggregate_return_nonempty.cc && /tmp/t    # exit 0
  ./m++ --specs=host -o /tmp/t test/cpp/return_pair_value.cc && /tmp/t
  make check-cpp-func check-cpp-neg check-c-mir                                  # 全绿
  ```
- **依赖**：无（与 T02/T04 文件区无重叠）

#### T04. **requires 表达式四类需求**（覆盖补全）
- **区域**：`src/cpp/parse/cpp_parse.c` 概念体缓冲（`cpp_parse.c` `eval_constraint` 附近：4729-4900）+ `include/cpp.h`（已有 `cpp_requires_expr` 声明）
- **修改**：从 worker-req4 wip 分支 `c9ca880` cherry-pick（已实现的四类需求 + `test/cpp/requires_min.cc`），扩展为 `test/cpp/requires_variants.cc`（简单/类型/复合/嵌套四类全覆盖）+ `test/cpp/requires_in_concept.cc`（在概念体内调用 `requires`）
- **验收**：
  ```sh
  ./m++ --specs=host -o /tmp/t test/cpp/requires_variants.cc && /tmp/t
  ./m++ --specs=host -o /tmp/t test/cpp/requires_in_concept.cc && /tmp/t
  make check-cpp-func check-cpp-neg check-concepts
  ```
- **依赖**：T01（同一区域，但行号段不重叠：T01 在 4729-4830，T04 在 4830-4900）

#### T05. **CT-REQ 缺陷：类模板 + requires-clause 顺序 bug**（缺陷修复）
- **区域**：`src/cpp/parse/cpp_parse.c:3759`（is_class 检测）+ `:3822`（requires 消费）
- **修改**：调整顺序，类模板先消费 `template<...>` 头部 + `requires` 子句后再做 is_class 检测（或提前到缓冲阶段）
- **测试**：`test/cpp/class_tmpl_requires.cc`（`template<typename T> requires Concept<T> class C { ... };` + 实例化）
- **验收**：
  ```sh
  ./m++ --specs=host -o /tmp/t test/cpp/class_tmpl_requires.cc && /tmp/t
  make check-cpp-func
  ```
- **依赖**：无（小修）

#### T06. **NTTP 残留边界修复**（覆盖补全 + 缺陷修复）
- **区域**：`src/cpp/parse/cpp_parse.c` 模板参数循环（`cpp_template_decl` 附近 3713/5480）+ 实例化键编码
- **修改**：grace `ef89d22` 已实现依赖类型 NTTP；本任务处理边缘：
  - NTTP + 类模板 + 显式实参混合（如 `template<T,T N> class C; C<int,5> x;`）
  - NTTP + constexpr 折叠（NTTP 值在 static_assert 中求值）
  - NTTP + 函数模板默认值（部分支持）
- **测试**：`test/cpp/nttp_dep_type.cc` 扩展（已有 alice + grace 实现）→ 新增 `test/cpp/nttp_class_template.cc`（混合显式实参）+ `test/cpp/nttp_constexpr_fold.cc`
- **验收**：
  ```sh
  ./m++ --specs=host -o /tmp/t test/cpp/nttp_class_template.cc && /tmp/t
  ./m++ --specs=host -o /tmp/t test/cpp/nttp_constexpr_fold.cc && /tmp/t
  make check-cpp-func check-cpp-neg
  ```
- **依赖**：T01/T04 不依赖；T05 完成后更顺

#### T07. **H 缺陷：`c.A::f()` 限定调用绕过虚表**（缺陷修复，方案就绪）
- **区域**：`src/c/parse/expr_postfix.c:493`（`if (m->is_virtual)` 分支）
- **修改**：加 `&& !qualified` 守卫（`defect-h-fix.md` §修复方案已就绪，一行级改动）
- **测试**：`test/cpp/defect_h.cc` 已存在（基线用例），新增 `test/cpp/defect_h_threehop.cc`（`c.A::f()` / `c.B::f()` / `c.C::f()` 三层限定全静态绑定）+ `test/cpp/defect_h_this_arrow.cc`（方法体内 `this->A::f()`）
- **验收**：
  ```sh
  ./m++ --specs=host -o /tmp/t test/cpp/defect_h_threehop.cc && /tmp/t
  ./m++ --specs=host -o /tmp/t test/cpp/defect_h_this_arrow.cc && /tmp/t
  make check-cpp-virtual check-cpp-func
  ```
- **依赖**：无（小修；与 expr_postfix.c 其他 worker 协同即可）

#### T08. **D3 缺陷：`#elifdef` 前组求值误报**（缺陷修复）
- **区域**：`src/c/lex/pp.c` 跳过组内求值分支（`#elifdef`/`#elifndef` 处理点）
- **修改**：当跳过 `#if 0 … #elifdef FOO` 时，不要在求值模式下报告「expected newline after preprocessing directive」，应继续到下一个 `endif` 或下一组 `#elif`/`#else`
- **测试**：从 pending 移到 `test/c23/elifdef_boundary.c`（`#if 0 ... #elifdef X ... #endif`）+ `test/cpp/elifdef_macro.cc`（C++ 中带宏）
- **验收**：
  ```sh
  ./mcc --specs=host -o /tmp/t test/c23/elifdef_boundary.c && /tmp/t
  ./m++ --specs=host -o /tmp/t test/cpp/elifdef_macro.cc && /tmp/t
  make check-c23 check-cpp-func
  ```
- **依赖**：无（worker-pp4 在修，本任务接力完成验证/补全）

### P1 任务（重要现代语法 + C 缺陷修复，11 个任务）

#### T09. **CINIT 覆盖：`constinit` 关键字**（覆盖补全）
- **区域**：`include/cpp/cpp_tokens.h`（无 CPP_TCONSTINIT）+ `src/cpp/lex/cpp_scan.c`（keyword 列表）
- **修改**：lex 加 `constinit` → CPP_TCONSTINIT；declspecs 接受（与 constexpr 同级，强制编译期常值初值）
- **测试**：从 `test/cpp/constinit.neg.cc` 移正 → 新增 `test/cpp/constinit.cc`（`constinit int x = 7;` 通过 + 非常值报错）
- **验收**：
  ```sh
  ./m++ --specs=host -o /tmp/t test/cpp/constinit.cc && /tmp/t
  make check-cpp-func check-cpp-neg
  ```
- **依赖**：无

#### T10. **C8T 覆盖：`char8_t` 类型关键字**（覆盖补全）
- **区域**：`include/mcc.h`（typekind 缺 `TYPECHAR8`）+ `src/c/sema/type.c`（类型构造）+ `src/c/lex/scan.c:448-452`（u8 字面量类型推导）
- **修改**：typekind 加 TYPECHAR8（映射 MT_I8 / MT_U8）；declspecs 接受 `char8_t`；u8 字面量在 C++ 模式推导 `const char8_t[N]`（C23 模式按标准保持 unsigned char）
- **测试**：`test/cpp/char8.cc`（`const char8_t* s = u8"abc";` + `char8_t c = u8'x';` + `sizeof(char8_t)==1`）
- **验收**：
  ```sh
  ./m++ --specs=host -o /tmp/t test/cpp/char8.cc && /tmp/t
  make check-cpp-func check-c23
  ```
- **依赖**：无

#### T11. **EXPB 覆盖：`explicit(bool)` 条件**（覆盖补全）
- **区域**：`src/c/parse/specs.c`（`explicit` 处理）
- **修改**：识别 `explicit(expr)` 形式，expr 折叠为 bool，true 则该 ctor 为 explicit
- **测试**：`test/cpp/explicit_bool.cc`（`explicit(true)` / `explicit(false)` / `explicit(sizeof(T)==4)` 三档）
- **验收**：
  ```sh
  ./m++ --specs=host -o /tmp/t test/cpp/explicit_bool.cc && /tmp/t
  make check-cpp-func check-cpp-neg
  ```
- **依赖**：无（小改）

#### T12. **UENUM 覆盖：`using enum E;`**（覆盖补全）
- **区域**：`src/cpp/parse/cpp_parse.c` namespace-alias（`cpp_using_decl`）附近
- **修改**：识别 `using enum <class-enum-name>;`（cpp23-gaps.md §2 基线缺口），把 E 的所有枚举符注入当前作用域
- **测试**：`test/cpp/using_enum.cc`（`using enum Color;` 直接用 `Red` 不写 `Color::Red`）
- **验收**：
  ```sh
  ./m++ --specs=host -o /tmp/t test/cpp/using_enum.cc && /tmp/t
  make check-cpp-func
  ```
- **依赖**：无（小改）

#### T13. **LTPL 覆盖：lambda 模板参数**（覆盖补全）
- **区域**：`src/cpp/parse/cpp_parse.c` `cpp_lambda_expr`（primaryexpr 的 `[` 入口）附近
- **修改**：lambda 头解析支持 `[]<typename T>(params)` / `[]<int N>(params)`；operator() 合成函数模板
- **测试**：`test/cpp/lambda_tmpl.cc`（`[]<typename T>(T a, T b){ return a+b; }` + 调用点类型推导）
- **验收**：
  ```sh
  ./m++ --specs=host -o /tmp/t test/cpp/lambda_tmpl.cc && /tmp/t
  make check-cpp-func
  ```
- **依赖**：无

#### T14. **SSHIP-DFT 覆盖：类类型三向比较 `= default`**（覆盖补全）
- **区域**：`src/cpp/parse/cpp_parse.c:1162-1179`（`cpp_op_mangle`）+ `:1225`（TLBRACK 旁）
- **修改**：member function 定义解析接受 `operator<=> = default`（不含 `{}`）；按成员序合成逐字段比较；`a < b` 重写到 `(a <=> b) < 0`
- **测试**：`test/cpp/spaceship_default.cc`（`auto operator<=>(const S&) const = default;` + 比较）
- **验收**：
  ```sh
  ./m++ --specs=host -o /tmp/t test/cpp/spaceship_default.cc && /tmp/t
  make check-cpp-func
  ```
- **依赖**：T02（运算符形参正确后才好）

#### T15. **NOEX 基线：`noexcept` 限定符**（基线补全）
- **区域**：`src/c/parse/specs.c`（typequal）+ `src/cpp/parse/cpp_parse.c`（函数声明）
- **修改**：识别 `noexcept` / `noexcept(expr)`；函数 mtype 加 noexcept 标记（暂仅记录不参与重载/异常表生成）
- **测试**：`test/cpp/noexcept_spec.cc`（`int f() noexcept;` + `noexcept(f())` 常量表达式编译通过）
- **验收**：
  ```sh
  ./m++ --specs=host -o /tmp/t test/cpp/noexcept_spec.cc && /tmp/t
  make check-cpp-func check-cpp-neg
  ```
- **依赖**：无（cpp23-gaps.md §2 阻塞 P1102 子集）

#### T16. **EXTC 基线：`extern "C"` 连接说明**（基线补全）
- **区域**：`src/cpp/parse/cpp_parse.c` linkage-spec 解析
- **修改**：识别 `extern "C"` 函数声明与 `extern "C" { ... }` 块；mangle 关掉（用 C 名字）
- **测试**：`test/cpp/extern_c.cc`（`extern "C" int printf(const char*,...);` + 链接 meuos-libc）
- **验收**：
  ```sh
  MEUOS_SYSROOT=$(pwd)/../sysroot ./m++ --specs=meuos -o /tmp/t test/cpp/extern_c.cc && /tmp/t
  make check-cpp-func check-sysroot-static
  ```
- **依赖**：无（cpp23-gaps.md §2 基线缺口）

#### T17. **LAMBDA-CAP 覆盖：init-capture / 引用捕获 lambda**（覆盖补全）
- **区域**：`src/cpp/parse/cpp_parse.c` `cpp_lambda_expr` 捕获解析
- **修改**：支持 `[&x]` / `[&]` / `[x=expr]` 形式（roadmap §2.3 注明引用捕获需补）
- **测试**：`test/cpp/lambda_capture_ref.cc`（`[&x](){ x++; }()` 修改外部变量 + `[y=42](){ return y; }()`）
- **验收**：
  ```sh
  ./m++ --specs=host -o /tmp/t test/cpp/lambda_capture_ref.cc && /tmp/t
  make check-cpp-func
  ```
- **依赖**：无

#### T18. **C-LIT-F / C-LIT-U：C23 浮点后缀 + UINT64_MAX 字面量回退**（C 缺陷修复）
- **区域**：`src/c/lex/scan.c`（`number()` 函数）+ `src/c/lex/pp_expr.c:43`
- **修改**：
  - `100f` / `1e10f` / `.5f` / `1f` 等无小数点/指数的 float 后缀加入浮点后缀表
  - 十进制无后缀常量超过 LLONG_MAX 时回退到 unsigned long long（C 6.4.4.1）
- **测试**：
  - `test/c23/float_suffix_nodot.c`（`100f` / `1e10f` / `0x1fp10f`）
  - `test/c23/uint64_max.c`（`UINT64_MAX = 18446744073709551615ULL` 直接用）
- **验收**：
  ```sh
  ./mcc --specs=host -o /tmp/t test/c23/float_suffix_nodot.c && /tmp/t
  ./mcc --specs=host -o /tmp/t test/c23/uint64_max.c && /tmp/t
  make check-c23 check-chibicc    # chibicc generic.c/literal.c 转 PASS
  ```
- **依赖**：无

#### T19. **C-MACRO / C-LINE：C 宏重定义 + __LINE__ 偏差**（C 缺陷修复）
- **区域**：`src/c/lex/pp.c`（macro table + 行号计数）
- **修改**：
  - 宏重定义：相同 token 序列的 `#define M1 ...` 二次定义静默接受（warning 或 no-op）
  - `__LINE__` 偏差：行号递增点修正（从 token 起点改到行尾 / 修正计数边界）
- **测试**：
  - `test/c23/macro_redefine.c`（相同定义二次 `#define` 通过 + 不同定义报错）
  - `test/c23/line_basic.c`（`#line 100` 后 `__LINE__==100`）
- **验收**：
  ```sh
  ./mcc --specs=host -o /tmp/t test/c23/macro_redefine.c && /tmp/t
  ./mcc --specs=host -o /tmp/t test/c23/line_basic.c && /tmp/t
  make check-c23 check-chibicc    # chibicc macro.c/line.c 转 PASS
  ```
- **依赖**：无

### P2 任务（按需补全 + 性能优化，6+ 个任务）

#### T20. **DCLT 基线 + ENUMC + DFTMP + FOLD：cpp_scan.c 关键字 + pending 5 项批量落地**
- **区域**：
  - `include/cpp/cpp_tokens.h`（加 CPP_TDECLTYPE）
  - `src/cpp/lex/cpp_scan.c`（加 `decltype`/`enum class`/`fold` 关键字）
  - `src/cpp/parse/cpp_parse.c` 各对应解析点（decltype 表达式 / enum 作用域 / 模板默认实参 / 一元/二元/三元 fold）
- **修改**：批量从 pending/ 转正（`decltype.cc`/`enum_class.cc`/`default_tmpl_arg.cc`/`fold_expr.cc`/`auto_nttp.cc`/`uniform_init.cc` 部分），逐个写实现 + 测试
- **测试**：5 个新测试文件，对应 pending 转正
- **验收**：
  ```sh
  for t in decltype enum_class default_tmpl_arg fold_expr auto_nttp; do
    ./m++ --specs=host -o /tmp/t test/cpp/$t.cc && /tmp/t || break
  done
  make check-cpp-func check-cpp-neg
  ```
- **依赖**：无（建议拆 5 个 worker 各 1 项以充分并行）

#### T21. **OPT-A：参数寄存器 hint（最大杠杆性能优化）**
- **区域**：`src/mir/regalloc.c`（`mreg_slots` + 线性扫描 ABI 边界倾向）+ `src/c/irgen/func_to_mir.c`（参数表示）
- **修改**：参数按 ABI 约定映射到入参寄存器（x86_64 rdi/rsi/rdx/rcx/r8/r9；riscv64 a0-a7；aarch64 x0-x7）；循环深度权重参数永不溢出
- **测试**：`test/bench/intloop.c` + `test/bench/fp_mat.c` + `test/bench/strings.c` 加指令数断言
- **验收**：
  ```sh
  # 编译并统计指令数
  ./mcc --specs=host -S -o /tmp/asm.s test/bench/intloop.c
  objdump -d /tmp/asm.s | awk '/^[[:space:]]+[0-9a-f]+:/{c++}' END'{print c}'    # O2 应 < 433（bench-report.md 基线）
  ./test/bench/compare.sh 3
  make check-olevel check-mir-regalloc    # regalloc 单元测试
  ```
- **依赖**：无（独立 OPT 任务，但建议先于 OPT-B/C/D）

#### T22. **OPT-C：postra 冗余 mov 消除**（mov-coalescing）
- **区域**：`src/mir/regalloc.c`（postra pass）
- **修改**：复制传播 + 消除（`mov %rax,%r12; mov %r12,%rax` 折叠为 `mov %rax,%rax` 或直接删除）
- **测试**：bench 同上 + 新增 `test/olevel/mov_coalesce.c`
- **验收**：
  ```sh
  ./mcc --specs=host -S -o /tmp/asm.s test/bench/strings.c
  objdump -d /tmp/asm.s | awk '/^[[:space:]]+[0-9a-f]+:/{c++}' END'{print c}'    # 应 < 613
  make check-olevel check-mir-regalloc
  ```
- **依赖**：T21（同一文件但 pass 段不同，可并行）

#### T23. **OPT-E：div/rem-by-constant → multiply-shift**（MIR pass）
- **区域**：`src/mir/passes.c`（新 pass `mdiv_const`）
- **修改**：识别 `x/7` / `x%7` 等常量除法/取模，用 magic multiply 算法替换（参考 GCC/llvm 编译器魔法数表）
- **测试**：`test/olevel/div_const.c` + bench intloop.c
- **验收**：
  ```sh
  ./mcc --specs=host -S -o /tmp/asm.s test/olevel/div_const.c
  grep -c "^[[:space:]]*idiv" /tmp/asm.s    # 应为 0（无 idiv 指令）
  make check-olevel check-mir
  ```
- **依赖**：无

#### T24. **UX-A + UX-C：`-ftime-report` + `-save-temps`**（用户体验）
- **区域**：`src/driver/`（driver.c / main.c）
- **修改**：
  - `-ftime-report`：打印 lex/parse/sema/irgen/codegen 各阶段耗时
  - `-save-temps`：保留 `.i` / `.s` / `.o` 中间产物
- **测试**：`test/driver/cli-args.sh` 扩展（已有 alice 提交基础）
- **验收**：
  ```sh
  ./mcc -ftime-report -o /tmp/t test/c23/hello.c 2>&1 | grep -E "lex|parse|sema|irgen|codegen"
  ./mcc -save-temps -o /tmp/t test/c23/hello.c && ls -la /tmp/t.* /tmp/t
  make check-driver
  ```
- **依赖**：无

#### T25. **UX-F：check-pic-verify 修复**（riscv64/i386 GOT 序列）
- **区域**：`src/target/riscv64/riscv64_emit.c:234-258`（已有部分 GOT 序列，需补全）+ `src/target/i386/i386_emit.c`（GOT 序列缺失）
- **修改**：
  - riscv64：补全 `auipc %got_pcrel_hi` + `ld %pcrel_lo(label)` 序列（已部分实现，需覆盖所有 SExt 路径）
  - i386：补 `%ebx` PIC 基址保留 + `call __x86.get_pc_thunk.bx` + `R_386_GOTPC` + `R_386_GOT32X`（diana 6db1691 已修 i386 主路径，需核实是否所有 SExt 都走 GOT）
- **测试**：`test/pic_verify.sh` 已含 riscv64/i386；新增 `test/targets/pic_comprehensive.c`
- **验收**：
  ```sh
  ./mcc -fPIC -target riscv64 -S -o /tmp/asm.s test/targets/pic_comprehensive.c
  grep -q "got_pcrel_hi" /tmp/asm.s || echo FAIL
  ./mcc -fPIC -target i386 -S -o /tmp/asm.s test/targets/pic_comprehensive.c
  grep -q "@GOT" /tmp/asm.s || echo FAIL
  make check-pic-verify    # 解锁门禁
  bash test/verify-all.sh  # 包含 check-pic-verify
  ```
- **依赖**：无（解锁 verify-all 关键门禁）

---

## 3. 文件区域隔离表（多 worker 并行不冲突的关键）

> 同一 worker 不要跨区域；不同 worker 不要挤同一区段。

| 任务 | 主改文件 | 行号段（建议） | 关联文件 |
|:----:|:--------|:---------------|:---------|
| T01 | `src/cpp/parse/cpp_parse.c` | 4729-4830 | — |
| T02 | `src/cpp/parse/cpp_parse.c` | 1274-1339 | `include/cpp.h` |
| T03 | `src/c/irgen/func.c` | 全文件 | `src/c/irgen/value.c`、`src/c/irgen/expr.c` |
| T04 | `src/cpp/parse/cpp_parse.c` | 4830-4900 | — |
| T05 | `src/cpp/parse/cpp_parse.c` | 3700-3900 | — |
| T06 | `src/cpp/parse/cpp_parse.c` | 5400-5550 | — |
| T07 | `src/c/parse/expr_postfix.c` | 490-520 | — |
| T08 | `src/c/lex/pp.c` | 跳过组求值分支 | — |
| T09 | `include/cpp/cpp_tokens.h`、`src/cpp/lex/cpp_scan.c` | keyword 表 | — |
| T10 | `include/mcc.h`、`src/c/sema/type.c`、`src/c/lex/scan.c` | 448-470 | — |
| T11 | `src/c/parse/specs.c` | explicit 处理 | — |
| T12 | `src/cpp/parse/cpp_parse.c` | using_decl 附近 | — |
| T13 | `src/cpp/parse/cpp_parse.c` | lambda 入口 | — |
| T14 | `src/cpp/parse/cpp_parse.c` | 1162-1179 + 1225 | — |
| T15 | `src/c/parse/specs.c` | typequal | `src/cpp/parse/cpp_parse.c` |
| T16 | `src/cpp/parse/cpp_parse.c` | linkage-spec | — |
| T17 | `src/cpp/parse/cpp_parse.c` | lambda 捕获 | — |
| T18 | `src/c/lex/scan.c` | number() 后缀表 | `src/c/lex/pp_expr.c:43` |
| T19 | `src/c/lex/pp.c` | macro table + line counter | — |
| T20 | `include/cpp/cpp_tokens.h` + `src/cpp/lex/cpp_scan.c` + cpp_parse.c 5 处 | 拆分 5 worker | — |
| T21 | `src/mir/regalloc.c` | 参数 hint | `src/c/irgen/func_to_mir.c` |
| T22 | `src/mir/regalloc.c` | postra 段 | — |
| T23 | `src/mir/passes.c` | 新 pass | — |
| T24 | `src/driver/` | driver.c | — |
| T25 | `src/target/riscv64/` + `src/target/i386/` | emit.c GOT 序列 | — |

**共享头 `include/cpp.h`**：T02/T04/T09/T10/T13/T14/T16/T17/T20 任何新增全局 extern 必须先在此处加；建议集中 PR 或各 worker 提交前 `git diff include/cpp.h` 自查。

**提交纪律**（来自 worker-deployment.md §5）：文件级 `git add <file>` + `git commit --only <path>`，禁止 `git add -A`；遇夹带维持现状不 force push（已发生 3 次：647a05b/93ab4b4/6ca4ba1）。

---

## 4. 推荐并行派发顺序

**轮次 1（首批 8 worker 并行，P0 全推）**：

1. T01（concept 深度）—— lite worker（如 grace/2）
2. T02（D1 运算符形参）—— lite worker（如 chloe-2）
3. T03（D4 非空类返回）—— reasoning worker（如 alice —— 需要 ABI 知识）
4. T04（requires 表达式）—— lite worker（cherry-pick wip）
5. T05（CT-REQ 类模板 requires）—— lite worker（小修）
6. T06（NTTP 残留）—— lite worker
7. T07（H 缺陷一行级）—— lite worker（5 分钟即可）
8. T08（D3 #elifdef）—— lite worker

**轮次 2（P1 批量 11 个，可拆 11 worker）**：
- T09-T17（C++ 覆盖）+ T18/T19（C 缺陷）

**轮次 3（性能 + 体验优化，P2）**：
- T20（pending 批量）+ T21/T22/T23（性能 OPT）+ T24（UX）+ T25（PIC 门禁解锁）

> **每轮建议模型分布**：reasoning（贵）限 2 个 / 5h 计费（参考 `feedback_model_resources.md`）；lite(hy3) 免费可 10+ 并行。本计划 25 个任务中 P0 选 1 个 reasoning（T03 ABI 需要深度推理），其余全 lite。

---

## 5. 假设与风险

### 5.1 假设

1. **环境稳定**：meuos-sysroot 已构建（`projects/sysroot` 存在），可支持 `MEUOS_SYSROOT=$(pwd)/../sysroot` 模式（参考 verify-all.sh 默认探测）
2. **hy3 额度**：lite(hy3) 至少有 2 天可用（用户称"hy3 只剩 2 天"——本计划要求快速迭代 + 充分并行）
3. **可并行性**：6 分支归并先例（worker-deployment.md §3）已证明同 worktree 8+ worker 并行可行
4. **worktree-mxx-work 干净**：HEAD=3446850 工作树无未提交改动
5. **任务粒度**：每个任务可由 1 个 worker 在 4 小时内完成（cpp_parse.c 8146 行的区域定位可保证；性能 OPT 类可能需要更长时间，但有 bench-report.md 已有明确方向）

### 5.2 风险

| 风险 | 概率 | 影响 | 缓解 |
|:-----|:----:|:----:|:-----|
| **cpp_parse.c 单文件冲突** | 中 | 高 | 严格按行号段分配；提交前 `git status` 自查 + `git diff --stat` 确认 |
| **T03（非空类按值返回）实现复杂** | 高 | 中 | 已分配 reasoning worker；空类路径 2be27a7 可作参考 |
| **D2 急切实例化边界残留** | 中 | 中 | alice d28c744 已部分修；T09 派发时需先复现剩余边界 |
| **T20 pending 5 项某些隐藏依赖** | 中 | 低 | 5 worker 各自独立测；失败时不阻塞其他 4 项 |
| **performance 优化与现有 olevel check 冲突** | 中 | 中 | OPT 任务需在 `make check-olevel` 通过后才算完成；分配前看 bench-report.md 基线 |
| **pic-verify 修复需要新增 reloc 类型** | 低 | 中 | T25 worker 需熟悉 R_386_GOTPC/R_386_GOT32X/R_RISCV_GOT_HI20 等 |
| **M 缺陷（未命名参数 ctor）可能与 T02 撞区** | 低 | 低 | T02 区段 1274-1339；M 缺陷位置待定位（grep 命名 ctor），如撞则串行执行 |

### 5.3 不在本次计划范围（明确推迟）

- **coroutines / modules**：明确不支持（需后端状态机 / 分离编译改造；worker-deployment.md §4 已确认）
- **C++23 P2280/P2448 完整 constexpr 引用/数组**：阻塞 D4，依赖 T03 完成后才能评估
- **check-chibicc 全绿（41/41 PASS）**：26 项 GNU 扩展（`({})`/`alloca`/`asm`/`__attribute__`）属合理不支持；8 项真实缺陷可由 T18/T19/T06 等逐步消化；其余测试自身偏差不属 mcc
- **mcc 与 GCC 性能平起平坐**：bench-report.md 13-32x 差距是巨大工程，本计划 OPT-A/B/C/D/E/F/G 是定向优化不等于追平 GCC

---

## 6. 验收与汇报

**每个任务的最小汇报模板**（发给 team-lead）：

```
任务 [TXX]: <标题>
提交: <commit hash>
文件: <改动的文件:行号>
测试: <新增/修改的测试文件>
验收命令: <实际跑通的命令 + 输出>
verify-all: <影响到的门禁状态>
风险: <任何遗留 / 已知问题>
建议: <下一轮可启动的后续任务>
```

**整体汇报触发点**（planner → team-lead）：
- 计划文件落地（本文档）
- 每轮 worker 派发建议（轮次 1/2/3）
- 任何阻碍门禁（verify-all.sh FAIL）的发现
- 跨任务依赖触发（如 T03 完成后才能评估 P2280/P2448）

---

> 计划文件路径：`/workspace/MeuOS-Kit/.agents/plans/r7-iteration-plan.md`
> 工作树：`.agents/worktrees/mxx-work/`（HEAD `3446850`，干净）
> 调研日期：2026-08-03
> 调研人：planner（reasoning 模型）
