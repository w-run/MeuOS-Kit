# MeuOS Kit 团队上下文交接（手工快照）

> 用途：留档当前多 worker 团队阶段状态，供会话重启或有新 worker 加入时快速接续。
> 记录时间：2026-08-05 晚（本阶段收束点）。权威状态以 git + `.todo/` + 各组件 `ARCHITECTURE.md` 为准。
> 分支：main 单源 `03de06a6`，mcc-toolchain / fix/mt-work / merge/alarm 三分支均领先 main=0（无待合）。

## 当前全局

- **main = `03de06a6`**（三方 worker 成果全部合入，单源同步）。
- **三 worker 全部待命**（本阶段收束）。团队：mcc-worker / mt-worker / libc-worker。
- 递归自检 job 已停（大喵要求 optimize token：取消分钟级 goal 循环，宏观汇报 1h）。

## 三 worker 本阶段交付盘点（均 verify-all 24/24 或 make check PASS 收束）

### mcc-worker（mcc-toolchain worktree）
- **跨架构修复 8 块**：arm i64 shift / arm rr_call / arm 常量物化 / i386 shift / i64 高半物化 / loongarch CFG(entry 跳 .bb0) / pcalau12i 地址对齐 / i386 rr_call+sret。
- **前端深面**：宽字面量 `L".."` / 类模板偏特化(T*多pattern排序+指针mangle碰撞修) / 变参核心(sizeof...计数+空包) / **m++ 深面① NTTP 调用折叠** + **② constexpr 成员函数调用折叠**。
- **收束点**：mcc 一致性专项 **task56** 登记（含 3 类跨层一致性深根：i386 rr_i64 QBE slot-half / 变参递归 overload+嵌套绑定 / constexpr mini-memory-model 深化）。i386 用 QBE 风格后端（i386_isel/memit）。
- 待命：task56 专项排期或大喵下阶段方向后继续。

### mt-worker（mt-work worktree，已固定单一 worktree）
- **跨架构矩阵**：6 架构 8 特性程序矩阵门禁 + 多个 mt/as 编码 bug 修复（arm 寄存器移位 / aarch64 movk lsl / riscv64 li64 / i386 shll/SSE2 / i386 srai funct6）+ **riscv64 reloc 重构** + loongarch pcaddu12i + CFG 复测。
- **矩阵收敛 32→2**：x86_64/arm/aarch64/riscv64 **8/8 硬 PASS** + i386 7/8 + loongarch64 6/8 + **hardFAIL=0**。mt/as+ld 侧清零。
- 剩 2 xfail 全 mcc 域：i386 rr_i64（QBE 槽位）+ loongarch rr_fp。
- 待命：loongarch P/place 深修（低优先专项）、riscv64/loongarch64 后续。

### libc-worker（libc-work worktree，merge/alarm 分支）
- **region1-4**：locale(strcoll/strxfrm) / stdio(clearerr+写error标志缺陷修) / math(反三角/memccpy/putenv) / wchar(btowc/wctob/mbrlen/wcwidth)。PASS 73。
- **门禁 batch1-8**：strtok/strtoull/truncate/snprintf/timebreak/syscfg/signal/stdio_file/procwait/errno 独立 gate + **strtoul 溢出缺陷修复**。
- **纪律**：install 总是 `DESTDIR=$(SYSROOT)`（不污染宿主机 /usr/lib），固定 worktree，自我 in-flight 记忆 gap 已用"author=w-run 自查"纪律。
- 待命：等 mcc 一致性专项后深化 wchar 或新方向。

## 下阶段专项候选（已登记 .todo/）

1. **mcc 一致性专项 task56**：i386 rr_i64（QBE slot-half 统一）/ 变参递归（overload ranking + 嵌套 pack 绑定）/ constexpr mini-memory-model 深化。
2. **loongarch64 rr_fp**（FP 定位/环境）。
3. **verify-all 脚本并行竞态**（偶发 FAIL=2，单独重跑稳定 24/24）。
4. **loongarch P/place 深修**（mt/ld relocation 深面，低优先）。
5. libc 深化：wchar 完整化（towctrans/wcwidth 表，待 mcc 宽字面量 done 后）、locale 多语言（大改，低优先）。

## 环境 / 纪律要记住

- **固定 worktree**：mcc→mcc-toolchain、mt→mt-work（已固定单一）、libc→libc-work。每 worker 独占。
- **libc 不污染宿主机**：make install 永远 `DESTDIR=projects/sysroot`。
- **worker 自我 in-flight 记忆 gap**（反复出现 worker 忘了自己 commit 误认为并行）——遇「worktree 有非我记得改动」先查 `git log --author=w-run` 再判断。
- **主 checkout 保持 main、干净**；工程源码在 HEAD 完整。
- **token 纪律**（大喵 optimize）：无分钟级递归 job，宏观汇报 1h，DS(DeepSeek) 余额 ¥46.8 为主力，reasoning(Ark 月14%) 禁用，MiniMax M3 周 47% 可临时切。
- **verify-all 并行竞态**：偶发 FAIL=2，单独重跑 24/24，非 code 回归。

## 交接要点

- 会话重启后可让 worker 读本文件 + mkit-session-import 恢复各自 jsonl（若 available）+ git status 核对。
- 三 worker 均 clean 待命，接续 = 从对应专项/方向继续推进。
- main 已是本阶段最大收敛（矩阵 32→2、m++ 深面①+②、一致性专项登记），下一步好起点是 **排 mcc 一致性专项 或 大喵指定新方向**。
