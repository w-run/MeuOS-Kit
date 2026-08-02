# MeuOS-Kit 会话 Worker 部署记录

> 更新：2026-08-02。记录 mcc/m++ 重构会话中 worker 的部署/角色/状态，供后续会话恢复上下文。

## 团队架构（语义化管理）

当前团队：`_auto_ad39aaae`（本会话活跃团队）。worker 按角色分型：

| Worker 名 | 角色 | 职责 | 状态 |
|---|---|---|---|
| planner | 规划型 | 参考实现探索 + 社区调研 + 可行性分析 | ✅ 完成（cpp-roadmap.md e745f17） |
| auditor | 验收检查型 | 代码验证 + 质量检测 + 审计 | ✅ 首轮闭环（25254fe→6003f47） |
| worker-cpp | 工作型（m++ 前端） | C++ 特性实现 | 🔄 constexpr 半成品已 stash |
| worker-toolchain | 工作型（6 架构/mt-as） | 架构后端 + 汇编器修复 | 🔄 arm c99-float 在途已 stash |
| worker-libc | 专项（libc 补全） | C99 标准库缺口 | ✅ C99 面完成（11 提交） |
| worker-test | 专项（测试矩阵） | 测试扩充 + 缺陷发现 | ✅ 6 批完成 |
| doc-sync | 文档型 | docs/.issues/ARCHITECTURE 同步 | ✅ 持续维护 |
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
| constexpr | — | 🔄 半成品 stash@{0}（gp-3 网络失败遗留） |

## libc 补全（worker-libc，11 提交）

转换：strtof/atof/atol/atoll/div/ldiv/rand/srand/mblen 家族/getppid；printf %f/%e/%g/%a（glibc 一致）+ 舍入修复；scanf 整数/浮点/长度修饰；time.h clock/asctime 等；Makefile .DEFAULT_GOAL 修复。
C99 剩余缺口：strtold/%Lf（mcc 无 long double）、i386 完整构建（Kl flagislt）。

## 缺陷队列（待修）

| 编号 | 缺陷 | 状态 |
|---|---|---|
| B | m++ 自由函数重载被拒 | 待修（worker-cpp） |
| C | 继承析构链缺失 | 待修 |
| D | static void 方法误判构造 | 待修 |
| E | ns 四项限制 | 待修 |
| A/B | mcc atomic FAIL、fscanf 栈布局挂死 | 待排查 |
| F | MIR fold shl/sar(x,0) 优化缺口 | 低优先 |
| i386 | Kl flagislt/flagiult 未支持 | 待修（worker-toolchain） |

## stash 说明

- stash@{0} `constexpr-wip+gp2-inflight`：gp-3 constexpr 半成品 + gp-2 arm/i386 在途（网络失败遗留，新 worker 恢复用）
- stash@{1} `gp2-inflight`：早期审计相关 stash
- pending/：14 个复现测试文件（未跟踪，worker-test 产出）

## 会话注意

- 提交纪律：C++ 完成前仅推 worktree-mxx-work，不合并 main
- 文档纪律：技术提交后 doc-sync 同步三份文档（ARCHITECTURE/roadmap/0802）
- 环境：worktree projects/sysroot 有 libc-meuos.a；check-c99 默认 specs 需它
