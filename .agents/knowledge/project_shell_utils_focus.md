---
name: Shell + Utils 现代优先哲学（重大方向调整）
description: 2026-07-31 大喵明确要求：完全实现两项目；现代优先+兼容层叠加；不做"又一个GNU"
type: project
---

**今日任务（2026-07-31）：将 meuos-shell(msh) 和 meuos-utils 完全实现**。

**Why**: 大喵明确指示——不局限于 GNU 风格，先实现**现代化的核心**（按 MeuOS Next 实际需求），然后在独立层做 GNU 兼容；守住核心别被 GNU 同化。两个项目都要 100% 实现（含 shell 的 POSIX sh + bash 兼容 + zsh 插件/主题 + YAML 配置）。

**核心哲学**：
- **现代优先**：每个工具/特性先按"我们究竟需要什么"实现；
- **GNU 兼容层叠加**：兼容性作为可选层（`--classic` 或 `--gnu` 开关），不污染核心；
- **风格**：简约、实用、现代化、模块化、用户体验；
- **绝对禁止**：实现"又一个 GNU 工具"——避免被 GNU 同化是核心价值观。

**How to apply**:
- **meuos-utils** 模块升级：
  - libutils.a 增加现代化组件：color / progress / icons / table / json-output / config / syntax（轻量着色）
  - 现代工具：ls（树状+图标+color）、cat（bat-lite）、find（fd 风格）、grep（ripgrep-lite）、diff（delta-lite）、cp/mv/rm（进度+安全+trash）、tree、wc/head/tail/sort/uniq/tr/cut/du
  - GNU 兼容通过 `--classic` / `--gnu` 开关启用
- **meuos-shell (msh)** 全面引擎：
  - 真 POSIX sh 词法/语法（AST 驱动）+ bash 关键扩展（数组/[[ ]]/brace/{source}）+ zsh 插件/主题钩子
  - YAML 配置：`~/.config/msh/config.yaml` 或 `.mshrc.yaml`
  - 插件系统：precmd/preexec/chpwd 钩子
  - 持久历史：`~/.local/share/msh/history`
  - 交互层：行编辑 + 高亮 + 自动建议
- **不依赖外部库**：纯 C + mcc + meuos-libc + meuos-toolchain
- **遵循 §4 强约束**：零 glibc/GNU/autotools/autoconf；零 GNU 代码复制

**会话语境**: worktree-shell-utils 分支，从 main 拉出；已 1 个领先提交推送；AGENTS.md §10.2 已同步骨架状态。
- 工作纪律：每任务 make check 通过→提交→不合并 main（按 §0 规约）
- 提交粒度：每工具/每大模块独立原子提交

**当前进度**（2026-07-31 上午）：
- ✅ .issues/AGENT.md + INDEX.md 已建
- ✅ 骨架阶段两项目 make check 烟雾通过（meuos-utils 8/8、meuos-shell 3/3）
- 🟡 升级阶段：执行 Task #6~#21
