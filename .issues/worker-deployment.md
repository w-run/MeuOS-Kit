# MeuOS-Kit 会话 Worker 部署记录

> 更新：2026-08-03。记录 mcc/m++ 重构会话中 worker 的部署/角色/状态，供后续会话恢复上下文。

## 当前团队：mcc-team-0599（2026-08-03 接管）

承接 `mcc-mxx-cont` 收官成果（A-N 缺陷全闭环），专注缺陷 Q/R/S/T 修复 + 第二批专项。权威进度在 `.issues/0802.md`「mcc-team-0599 部署与缺陷状态」。

### 第一批 worker（缺陷修复 + 文档）
| Worker | 任务 | 缺陷 |
|---|---|---|
| worker-q | delete/delete[] nullptr 段错误修复 | Q（✅ closed，f8f0044） |
| worker-r | concept 形参名 ≠ T 误判 undeclared 修复 | R（✅ closed，93ab4b4） |
| worker-lambda | lambda 捕获（拷贝 ctor + 嵌套捕获） | S+T（✅ closed，f8f0044 混入） |
| worker-doc | 文档同步（.issues/docs/ARCHITECTURE，只改 .md） | — |

### 第二批 worker（专项）
| Worker | 任务 |
|---|---|
| worker-test | 测试回归（verify-all 保持绿 + canary 回归） |
| worker-judge | reasoning 裁判整合 |
| worker-va | MIR va_list 溢出路径 |
| worker-fold | MIR fold 优化专项（F 闭环验证 ✅ + 发现缺陷 V） |
| worker-slot2 | slotmerge 二期方案（J 项替代） |
| worker-cpp20/23 | C++20/23 缺口调研 |
| worker-selfhost | 自举门禁（mcc 自编译 m++） |
| worker-mir-tests | MIR/C 测试缺口（发现缺陷 W/X） |

### 缺陷队列（2026-08-03；编号迁移为组件前缀+hex 见下）
> **编号体系**：2026-08-03 起缺陷编号为「组件/阶段前缀 + 两位 hex」——`cpp-`（C++ 前端）、`c-`（C 前端）、`mir-`（MIR）、`x86-`（x86_64 后端）；旧字母保留对照，历史章节标题已在 0802.md 标注新编号。

- **closed**：cpp-01(B,83db5ff)、cpp-02(C,c19a351)、cpp-03(D,16f1948)、cpp-04(E,6f3d734)、cpp-05(G,3f0ed41)、cpp-06(H,a096b52)、cpp-07(K,2755fe3)、cpp-08(M,4d93a66)、cpp-09(N,754b437)、cpp-0a(Q,f8f0044)、cpp-0b(R,93ab4b4)、cpp-0c(S,f8f0044)、cpp-0d(T,f8f0044)、cpp-0e(Z/U,2be27a7 LIR+e4a885c+00ed62b MIR 后端,empty_class_value.cc 双路径闭环)、mir-00(F,647a05b)、mir-01(V,93ab4b4+4c24bfe)、x86-00(va_list,222a28d)、range-for(71fbb35)
- **open**：cpp-0f(Y, delete (T*)expr, low)、c-00(W, u8 字面量)、c-01(X, inline+extern)
- **禁用**：mir-02(J, slotmerge 自举破坏)、mir-03(I, 并入 J)、cpp-00(A, 已废弃被 cpp-0e 替代)
- 状态只标 open/pending，修复 push 后由 worker-doc 周期 pull 补记 closed + 哈希。

## 团队架构（语义化管理）

当前团队：`mcc-mxx-cont`（mcc/m++ 重构团队，接管双覆盖收官；原 `_auto_ad39aaae` 已完成前序里程碑）。worker 按角色分型：

| Worker 名 | 角色 | 职责 | 状态 |
|---|---|---|---|
| planner | 规划型 | 参考实现探索 + 社区调研 + 可行性分析 | ✅ 完成（cpp-roadmap.md e745f17） |
| auditor | 验收检查型 | 代码验证 + 质量检测 + 审计 | ✅ 首轮闭环（25254fe→6003f47） |
| worker-cpp | 工作型（m++ 前端） | C++ 特性实现 + m++ 边界补全 + P2 spill slot 复用 | 🔄 m++ 边界 4 项完成（40d46f2/1eb76da/35a6ede/ecc42cf），P2 slotmerge 实施中 |
| worker-toolchain | 工作型（6 架构/mt-as） | 架构后端 + 汇编器修复 | ✅ 本团队前序完成（C23 补全/验收基线，已移交） |
| worker-libc | 专项（libc 补全） | C99 标准库缺口 | ✅ C99 面完成（11 提交） |
| worker-test | 专项（测试矩阵） | 测试扩充 + 缺陷发现 | ✅ 6 批完成（本团队新加入，后续可扩充回归用例） |
| doc-sync | 文档型 | docs/.issues/ARCHITECTURE 同步 | 🔄 本团队新加入，0802 里程碑收尾同步中 |
| worker-verify | 工作型（后备） | 验收门禁 verify-all | ✅ 完成（c940c34） |

## 早期 worker 名与角色映射（无法改名，按角色引用）

| 框架默认名 | 承担角色 | 说明 |
|---|---|---|
| general-purpose-1 | worker-verify | verify-all.sh 产出者 |
| general-purpose-2 | worker-toolchain | aarch64 cset/varargs/arm/i386 |
| general-purpose-3 | worker-cpp | auto/变参/lambda/constexpr |
| general-purpose-5 | doc-sync | ARCHITECTURE/roadmap/0802 |

## 本会话里程碑（m++ C++11 核心，按 roadmap）

| 特性 | 提交 | 状态 |
|---|---|---|
| 函数模板 | 84727a6 | ✅ |
| 类模板 | 642574b | ✅ |
| 成员模板 | c93d5f7 | ✅ |
| auto/decltype | 160e2a2 | ✅ |
| 变参模板 | df0c489 | ✅ |
| lambda | 877beed | ✅ |
| constexpr | 3ac233b | ✅ |

## libc 补全（worker-libc，11 提交）

转换：strtof/atof/atol/atoll/div/ldiv/rand/srand/mblen 家族/getppid；printf %f/%e/%g/%a（glibc 一致）+ 舍入修复；scanf 整数/浮点/长度修饰；time.h clock/asctime 等；Makefile .DEFAULT_GOAL 修复。
C99 剩余缺口：strtold/%Lf（mcc 无 long double）、i386 完整构建（Kl flagislt）。

## 缺陷队列（B/C/D/E 已闭环）

| 编号 | 缺陷 | 状态 |
|---|---|---|
| B | m++ 自由函数重载被拒 | ✅ 83db5ff |
| C | 继承析构链缺失 | ✅ c19a351 |
| D | static void 方法误判构造 | ✅ 16f1948 |
| E | ns 四项限制 | ✅ 6f3d734 |
| A/B | mcc atomic FAIL、fscanf 栈布局挂死 | ✅ 已闭环（840256c 判定不复现） |
| F | MIR fold shl/sar(x,0) 优化缺口 | 低优先（未修） |
| i386 | Kl flagislt/flagiult 未支持 | ✅ 400e0df |
| MIR 后端 | va_list 溢出路径（va_arg/vastart） | 🔄 待 MIR 后端默认化前处理 |

## stash 说明

- stash@{0} `constexpr-wip+gp2-inflight`：历史遗留。constexpr 已由 3ac233b 重新实现提交，gp-2 在途已恢复提交（bf39451/400e0df/arm 修复）；stash 内容已过时，可清理。
- stash@{1} `gp2-inflight`：早期审计相关 stash
- pending/：10 个复现/待处置测试文件（未跟踪，worker-test 产出）。B/C/D/E 转正后 ctor_base_dtor/static_void_method/free_func_overload/ns_limits 已移出到 test/cpp/；余下含 value_param_member_call（缺陷 A 不复现可转正向）等。

## 会话注意

- 提交纪律：C++ 完成前仅推 worktree-mxx-work，不合并 main
- 文档纪律：技术提交后 doc-sync 同步三份文档（ARCHITECTURE/roadmap/0802）
- 环境：worktree projects/sysroot 有 libc-meuos.a；check-c99 默认 specs 需它
