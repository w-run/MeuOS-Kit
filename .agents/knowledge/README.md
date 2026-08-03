# MeuOS Kit — 项目知识库（knowledge）

> 本目录沉淀自 AI 会话记忆（2026-08-04），供后续 Agent 会话快速获取项目经验，避免重复踩坑。
> 内容随项目 git 版本管理。会话结束后如有新的经验，应同步沉淀到这里。

## 分类

- **feedback_*** — 工作纪律与操作教训（git 安全、并发、构建、通知等）
- **project_*** — 项目技术经验（缺陷闭环、修复方案、架构决策）

## 索引

### Git 与并发纪律

| 文件 | 要点 |
|:-----|:-----|
| [feedback_commit_discipline.md](feedback_commit_discipline.md) | 多 worker 并发必须文件级 git add + `git commit -- <path>`；已三次夹带事故；遇夹带维持现状不 force push |
| [feedback_shared_worktree_concurrency.md](feedback_shared_worktree_concurrency.md) | 共享 worktree 上 reset/stash 会与并发进程交错；先查 ps/reflog 再操作；stash 全局共享 |
| [feedback_mainline_concurrent_verify.md](feedback_mainline_concurrent_verify.md) | 主线高速推进下判断纪律：复跑失败先 fetch 对齐 HEAD，再疑回归 |
| [feedback_bella_worktree_stale_concurrent.md](feedback_bella_worktree_stale_concurrent.md) | worktree 跨分支切换后 build/ 陈旧，改动源码后必须 make clean |
| [feedback_push_main_on_merge.md](feedback_push_main_on_merge.md) | main 有合并/提交就 push origin；daily-audit 永不合并 main |
| [feedback_notify.md](feedback_notify.md) | 里程碑推送；icon 必须 https PNG；push.py 不编码 icon 致 404；正文禁裸斜杠 |

### 编译器缺陷闭环（mcc/m++）

| 文件 | 要点 |
|:-----|:-----|
| [project_mir_sdiv_fix.md](project_mir_sdiv_fix.md) | MIR 有符号 div/rem 缺陷 V 闭环（93ab4b4 + 4c24bfe） |
| [project_mir_backend_empty_abi.md](project_mir_backend_empty_abi.md) | MIR 后端空类/混合参数 ABI 三处修复 |
| [project_mir_mem2reg_bridge_phi.md](project_mir_mem2reg_bridge_phi.md) | mem2reg/LOADFWD 分工；bridge.c phi 链式追加必保 |
| [project_chibicc_bclass_fix.md](project_chibicc_bclass_fix.md) | chibicc B 类修复（窄化 cast 不截断 + va_end 过严） |
| [project_mcc_chibicc.md](project_mcc_chibicc.md) | chibicc 套件根因（run.sh sysroot 路径 bug） |
| [project_sema_defects_e1e6.md](project_sema_defects_e1e6.md) | sema 组 E1-E6 修复；mcc struct array.len 是字节数 |
| [project_cpp23_gaps.md](project_cpp23_gaps.md) | C++23 四缺口闭环（P0849/P1774/P1401/P2360 + nodiscard） |
| [project_cpp_remaining_gaps.md](project_cpp_remaining_gaps.md) | 依赖 NTTP/constexpr 返回类对象/consteval 模板边界 |
| [project_mxx_defects_kmn.md](project_mxx_defects_kmn.md) | mxx 缺陷 K（concept 深度）/M（未命名参数 ctor）/N（数组 new stride） |
| [project_mxx_t02_unrelated_defects.md](project_mxx_t02_unrelated_defects.md) | T02/D1 之外 m++ 预存在缺陷 |
| [project_d3pp_elifdef.md](project_d3pp_elifdef.md) | D3-PP #elifdef 修复藏在 6ca4ba1；派任务前先复现确认 |
| [project_d4_t03_done.md](project_d4_t03_done.md) | D4 非空类按值返回已闭环（608f31c 修 emit_base_ctors_for） |
| [project_mcc_o0_ub.md](project_mcc_o0_ub.md) | mcc -O0 构建触发预存在 UB；门禁用默认 O2 |
| [project_slotmerge_selfhost.md](project_slotmerge_selfhost.md) | slotmerge 破坏自举（缺陷 J），已禁用 |
| [project_mxx_audit_regression.md](project_mxx_audit_regression.md) | mxx-work 审计回归记录 |
| [project_mxx_worktrees.md](project_mxx_worktrees.md) | mcc-toolchain 工作树布局；共享 worktree 并发/验证竞态经验 |

### 其他项目经验

| 文件 | 要点 |
|:-----|:-----|
| [project_libc_c99.md](project_libc_c99.md) | meuos-libc C99 补全；mcc 无 long double、i386 阻塞项 |
| [project_meuos-libtui.md](project_meuos-libtui.md) | 纯 C11 TUI 库 |
| [project_shell_utils_focus.md](project_shell_utils_focus.md) | meuos-shell + meuos-utils 实现焦点 |
| [project_autonomous_features.md](project_autonomous_features.md) | 自主特性优化方向（不做缝合怪） |
| [project_merge_mir_pic_got.md](project_merge_mir_pic_got.md) | 4 分支归并 + MIR PIC GOT |
| [project_merge6_olevel_cleared.md](project_merge6_olevel_cleared.md) | 6 分支归并 + olevel 清零 |
| [project_alice_stash_rref_vaarg.md](project_alice_stash_rref_vaarg.md) | alice 在途 stash（rvalue ref + va_arg） |
