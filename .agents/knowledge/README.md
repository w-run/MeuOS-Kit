# MeuOS Kit — 项目知识库（knowledge）

> 本目录沉淀自 AI 会话记忆（2026-08-04），供后续 Agent 会话快速获取项目经验，避免重复踩坑。
> 内容随项目 git 版本管理。会话结束后如有新的经验，应同步沉淀到这里。

## 索引

| 文件 | 分类 | 要点 |
|:-----|:----:|:-----|
| [feedback_git.md](feedback_git.md) | 纪律 | git 工作纪律：文件级 add + `commit -- <path>`、共享 worktree 禁 stash/reset、主线先 fetch 再判、build 陈旧 make clean、main 合并即推送、verify-all 并发竞态 |
| [feedback_notify.md](feedback_notify.md) | 纪律 | notify 推送：icon 必须 https PNG、push.py 不编码 icon 致 404、正文禁裸斜杠 |
| [project_mcc_mir.md](project_mcc_mir.md) | 缺陷 | MIR 后端：有符号 div/rem (V)、空类 ABI (U)、mem2reg/LOADFWD 分工 + bridge 多 phi、slotmerge 自举 (J) |
| [project_mcc_cpp.md](project_mcc_cpp.md) | 缺陷 | m++ 前端：C++23 四缺口、依赖 NTTP/constexpr 返回/consteval、缺陷 K/M/N、D4 非空类返回、-O0 UB |
| [project_mcc_chibicc.md](project_mcc_chibicc.md) | 缺陷 | chibicc 套件根因、B 类修复、sema E1-E6 + array.len 字节坑、D3-PP #elifdef、审计回归 |
| [project_meuos.md](project_meuos.md) | 项目 | 组件经验：libc C99 补全、libtui、Shell/Utils 现代优先、自主特性方向、归并记录 |
| [project_cpp_parse_split.md](project_cpp_parse_split.md) | 项目 | cpp_parse.c 拆分方法论：6 刀已拆(9088→6489)、跨文件函数/类型处理、自举一致性坑(零回归须跑 verify-all 的 check-sysroot-static) |
| [project_mcc_toolchain.md](project_mcc_toolchain.md) | 缺陷 | mcc-toolchain P0 闭环：toolchain 3 门禁 + ARM 正则、mcc MIR 文档清扫、i386 PIC GOT（4 提交）、ld.so P0 实施 in progress |
| [feedback_aarch64_aapcs.md](feedback_aarch64_aapcs.md) | 缺陷 | aarch64 AAPCS64 三处缺陷闭环：selpar off=0、大帧 stp 超限、spill-slot 基址寻址丢 x29 |
| [project_mcc_mxx_rebase.md](project_mcc_mxx_rebase.md) | 项目 | mxx-work 残留 rebase 无损清理纪律；lead-doc-mir-baseline 实为 m++/MIR 主线（LIR 桥接移除/mcombine/cpp class/双二进制） |
| [project_mcc_toolchain_roundup.md](project_mcc_toolchain_roundup.md) | 项目 | 本轮进展：TLS 静态 GD 闭环(方案B ae88aa1)、m++ C++ P1 5 项、P0.1 动态 libc(d224248)、mt/ld .dynamic 新阻塞、静态数组 segfault(已闭环)、教训 |
| [feedback_worker_per_component.md](feedback_worker_per_component.md) | 纪律 | 按组件分派专职 worker（mcc/libc/toolchain/meow），保持缓存和上下文持续，避免每次重读大段代码 |
| [project_mcc_static_global_array.md](project_mcc_static_global_array.md) | 缺陷 | x86_64 静态全局数组 segfault 闭环：根因 mt/as `imul $imm, %reg` 2-操作数编码缺失(dfcc0dc7)；回归测试(9e65b72d)；verify-all 门禁(9f1cf2be) |

## 使用约定

1. 新 agent 启动读本索引，按需打开对应文件。
2. 会话中产生新的可复用经验（踩坑教训、缺陷根因、修复方案），完成后沉淀为对应文件并更新本索引。
3. 本地 AI 记忆（`~/.codebuddy/projects/*/memory/`）与 `.agents/knowledge/` 分工：前者是会话级自动记忆，后者是项目级持久知识（进 git）。
