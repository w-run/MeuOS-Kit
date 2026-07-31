# worktree-shell-utils — 任务索引

> **本 worktree 范围**：从零实现 `projects/meuos-shell/`（msh，POSIX sh + 可选 bash 兼容）和 `projects/meuos-utils/`（coreutils/diffutils/findutils 等）
>
> **新 Agent 入口**：先读 `.issues/AGENT.md`
>
> **设计原则**：参见 `.issues/AGENT.md` § 设计原则段（§4 禁止事项 / §6.1 三阶段路径 / §6.2 参考资源）

---

## P0 — 骨架（必做，当前）

- [ ] **utils-skeleton** — `projects/meuos-utils/` 骨架建立（Makefile + ARCHITECTURE.md + src/common/ + include/ + 占位 main.c）
- [ ] **shell-skeleton** — `projects/meuos-shell/` 骨架建立（Makefile + ARCHITECTURE.md + src/ + include/msh/msh.h + 占位 main.c + --version）
- [ ] **doc-sync** — AGENTS.md §10.2 状态从「⏳ 待启动」改成「🟡 骨架完成」+ README.md 同步

## P1 — coreutils 之「最小 IO 集」

- [ ] **utils-cat** — `cat` 完整实现（stdin/多文件/- 选项/--help/--version）
- [ ] **utils-echo** — `echo` 完整实现（-n/-e/-E、POSIX 退格转义）
- [ ] **utils-true** — `true` 空命令（也作 libutils.a 的烟雾测试入口）
- [ ] **utils-false** — `false` 空命令
- [ ] **utils-yes** — `yes [str]` 无限输出
- [ ] **utils-test** — `test`/`[` 命令（POSIX 实现常用表达式）

## P2 — fileutils / coreutils 之「文件操作」

- [ ] **utils-cp** — `cp` 文件/目录复制（-r/-p/-f）
- [ ] **utils-mv** — `mv` 移动/重命名
- [ ] **utils-rm** — `rm` 删除（-r/-f）
- [ ] **utils-mkdir** — `mkdir` 创建目录（-p）
- [ ] **utils-rmdir** — `rmdir` 删除空目录
- [ ] **utils-ln** — `ln` 硬链接/符号链接（-s）
- [ ] **utils-touch** — `touch` 创建/更新时间戳
- [ ] **utils-ls** — `ls` 列目录（-l/-a/-h 等 GNU 子集）
- [ ] **utils-chmod** — `chmod` 改权限（符号 + 八进制）
- [ ] **utils-chown** — `chown` 改属主（可选，依赖 getpwnam）

## P3 — coreutils 之「文本处理基础」

- [ ] **utils-head** — `head` 前 N 行（-n/-c）
- [ ] **utils-tail** — `tail` 后 N 行（-n/-c/-f）
- [ ] **utils-wc** — `wc` 字节/行/词统计（-l/-w/-c/-m）
- [ ] **utils-sort** — `sort` 行排序（-r/-n/-u/-k）
- [ ] **utils-uniq** — `uniq` 去重（-c/-d/-u）
- [ ] **utils-cut** — `cut` 字段/字符切割（-d/-f/-c）
- [ ] **utils-tr** — `tr` 字符替换/删除（-d/-s）
- [ ] **utils-tee** — `tee` 双重输出（-a）
- [ ] **utils-dd** — `dd` 块复制（bs=/count=/skip=/conv=）

## P4 — diffutils / findutils / 文本处理

- [ ] **utils-diff** — `diff` 行对比（POSIX 子集）
- [ ] **utils-cmp** — `cmp` 字节对比
- [ ] **utils-patch** — `patch` 应用补丁
- [ ] **utils-find** — `find` 递归查找（-name/-type/-exec/-print 等）
- [ ] **utils-xargs** — `xargs` 参数化执行命令
- [ ] **utils-grep** — `grep` 模式匹配（POSIX BRE + 可选 ERE）

## P5 — 归档 / 压缩

- [ ] **utils-tar** — `tar` 创建/解包（POSIX pax 格式 + gzip 选项）
- [ ] **utils-gzip** — `gzip`/`gunzip` 压缩（基于 mz 库的 LZ77 或自主实现 DEFLATE）
- [ ] **utils-xz** — `xz`/`unxz` 压缩（如 MeOS 不实现则标记 stalled）
- [ ] **utils-zstd** — `zstd` 压缩（可选）
- [ ] **utils-zip** — `unzip` 解压（PKZIP，POSIX unzip 子集）

## P6 — msh POSIX sh 核心

- [ ] **msh-lex** — 词法：标识符/关键字/操作符/引号/转义/注释
- [ ] **msh-parse** — 语法：命令 + 管道 + 列表 + 复合命令
- [ ] **msh-exec** — 执行：execvp 集成 + 简单命令
- [ ] **msh-var** — 变量：定位参数 / `VAR=val` / `$VAR` `${VAR}` 展开
- [ ] **msh-expand** — 展开：glob（`?` `*` `[...]`）/ tilde / 命令替换 `$(...)` / 算术展开 `$((...))`
- [ ] **msh-redir** — 重定向：`>` `>>` `<` `2>` `&>`
- [ ] **msh-builtin** — 内建：`cd` `echo` `pwd` `export` `unset` `set` `exit` `:` `.` `exec` `read` `eval` `trap` `wait` `jobs`
- [ ] **msh-flow** — 控制流：`if`/`then`/`else`/`elif`/`fi` + `case`/`esac` + `for`/`while`/`until`
- [ ] **msh-func** — 函数定义与调用

## P7 — msh 交互层

- [ ] **msh-prompt** — PS1/PS2 提示符（含基本转义）
- [ ] **msh-history** — 历史记录（持久化 ~/.msh_history）
- [ ] **msh-lineedit** — 行编辑（vi/emacs 模式 + 字符级操作 + 删除/移动）
- [ ] **msh-tab** — Tab 补全（命令 + 路径 + 变量）
- [ ] **msh-job** — 作业控制（前台/后台 & + jobs/fg/bg）
- [ ] **msh-signal** — 信号处理（Ctrl-C → 终止前台，Ctrl-D → EOF）

## P8 — msh 可选扩展

- [ ] **msh-bashcompat** — bash 兼容层（数组 `${arr[@]}` / `[[]]` / `source` / `set -e`）
- [ ] **msh-zsh-plugin** — zsh 风格插件/主题（可选，依赖 zsh 生态或自主实现）
- [ ] **msh-completion-script** — 补全脚本加载（compctl/compgen）
- [ ] **msh-arrays** — bash 风格数组支持

---

## 设计原则（速查，与 AGENT.md 同步）

### 原则零：重新设计，不照搬 GNU

| 不要 | 要 |
|------|-----|
| "又一份 bash" | 给 MeuOS 定制的 POSIX sh 子集 + 选择性 bash 扩展 |
| "又一份 coreutils 全套" | 按 MeuOS 使用频次挑实现子集（cat/cp/mv/ls/find/grep 最先） |
| 每工具一份独立 Makefile 模板 | 复用 libutils.a 静态库（共通逻辑封装） |

### libutils.a 静态库策略

meuos-utils 所有工具共享 `libutils.a`：

| 单元 | 内容 |
|------|------|
| `xmalloc` / `xstrdup` / `xrealloc` | OOM 失败即退出 |
| `getopt_long` | GNU 长选项解析 |
| `progname` | argv[0] 提取工具名 |
| `version` | --version 输出版本 |
| `quote` / `escape` | shell 引用转义 |
| `human_readable` | 字节数 → "1.2K" 格式（ls -h 用） |
| `mbsalign` / `mb_width` | UTF-8 多字节对齐 |

### 分阶段提交

每完成一个工具：

1. `make -C projects/meuos-utils check` 通过
2. `git commit -m "utils: 实现 <toolname> (<task-id>)"` 提交
3. 更新本 INDEX.md 把任务从 `- [ ]` 改为 `- [x]`

---

## 阶段验收

| 阶段 | 验收 | 已知阻塞 |
|------|------|----------|
| P0 | 两组件骨架 `make check` 通过 | — |
| P1 | 5 个 IO 工具 + libutils.a 烟雾测试通过 | — |
| P2-P5 | 各工具独立 `make check` 通过 + 与 GNU 对应物行为对照测试 | 复杂工具（grep/diff）可能需要迭代 |
| P6 | msh 跑通 POSIX sh smoke 测试集（test/posix/smoke.sh） | 需要 §3 musl 已实现的 vfork/execve/waitpid |
| P7 | 交互层人工验证（env QEMU VM 内手测） | 需要 env/ QEMU 跑通 |
| P8 | 可选，按需推进 | 不阻塞 Phase 7 完成 |

