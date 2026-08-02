# MeuOS-Kit 会话 Worker 部署记录

> 更新：2026-08-02。记录 mcc/m++ 重构会话中 worker 的部署/角色/状态，供后续会话恢复上下文。

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
