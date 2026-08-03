---
name: chibicc 套件根因闭环
description: check-chibicc 曾"41 全灭"实为 run.sh sysroot 路径 bug，非编译器回归；修正后真实分布 PASS=9/RUNFAIL=6/COMPILEFAIL=26
type: project
---

`make check-chibicc`（test/community/chibicc，41 用例）曾显示 PASS=0/COMPILEFAIL=41 全灭，经三份独立调查（worker-gate3 / worker-verify2 / worker-chi4）**一致确认这是测试框架层 artifact，不是 mcc 编译器缺陷**。

**根因：** `test/community/chibicc/run.sh` 从脚本目录上跳 5 级推导仓库根 `ROOT=$ROOT`，再找 `$ROOT/sysroot/{x86_64,}`，但 worktree 的实际 sysroot 由 verify-all.sh 构建在 `projects/sysroot`（扁平 usr/ 布局），`$ROOT/sysroot` 不存在。静默传入坏 `--sysroot` 让每个用例链接期报 `cannot find -lc-meuos`，run.sh 又把链接失败计入 COMPILEFAIL → 套件伪装成"41/41 编译失败"。

**修复（commit f05633f，纯脚本、零编译器风险）：** sysroot 探测顺序 `$MEUOS_SYSROOT → $ROOT/projects/sysroot → $ROOT/sysroot/x86_64 → $ROOT/sysroot`（各级均要求 usr/include 存在），全部落空时显式 `exit 1` 响亮报错，不再静默传不存在的路径。

**修正后真实分布（三份调查互证）：PASS=9 / RUNFAIL=6 / COMPILEFAIL=26。**
- PASS(9): builtin complit generic line offsetof stdhdr typeof union usualconv
- RUNFAIL(6): bitfield cast commonsym decl float literal（其中 bitfield/cast/decl/commonsym 为真实 codegen/语义 bug，literal 实际 mcc 更符合标准）
- 26 个 COMPILEFAIL 分类：A 框架/测试移植缺口（pragma-once 路径、extern 缺定义文件、test.h 原型冲突）；B mcc 特性缺口（alloca/asm/\e/##__VA_ARGS__/_Noreturn/va_end 严检查/({})内 tag-only 声明）；C GNU 扩展宽松性（隐式 int、指针宽松赋值等，建议 xfail）；D 真实 bug（位域赋值、常量窄化截断、common 符号合并）

**Why:** 历史 REPORT.md 记载 PASS=7，后来 sysroot 挪到 projects/ 下脚本没跟着改，导致套件长期误报全灭，误导后续判断。三份调查用两套独立二进制+两套 sysroot 交叉验证排除构建差异。

**How to apply:** 不要再按"chibicc 全灭=编译器回归"处理。check-chibicc 不在 verify-all/check-all 内，仅 `make check-community` 单独触发，是非阻塞诊断。后续若恢复为门禁，建议：Makefile 显式传 MEUOS_SYSROOT 去歧义 + 引入 xfail 清单（宽容性用例）+ 真实 bug（bitfield/cast/decl/float/commonsym）转缺陷单逐个修 + 低成本特性（alloca 内置/asm 关键字/##__VA_ARGS__/\e）补上后判据可设 PASS≥20 且无新增 FAIL。完整分类见 /tmp/mcciso-chi4/REPORT-chi4-v2.md。
