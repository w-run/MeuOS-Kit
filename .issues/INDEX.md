# worktree-shell-utils — 任务索引

> **本 worktree 范围**：从零实现 `projects/meuos-shell/`（msh，POSIX sh + 可选 bash 兼容）和 `projects/meuos-utils/`（coreutils/diffutils/findutils 等）
>
> **新 Agent 入口**：先读 `.issues/AGENT.md`
>
> **设计原则**：参见 `.issues/AGENT.md` § 设计原则段（§4 禁止事项 / §6.1 三阶段路径 / §6.2 参考资源）

---

## P0 — 骨架（必做，当前）

- [x] **utils-skeleton** — `projects/meuos-utils/` 骨架建立（Makefile + ARCHITECTURE.md + src/common/ + include/ + 占位 main.c）
- [x] **shell-skeleton** — `projects/meuos-shell/` 骨架建立（Makefile + ARCHITECTURE.md + src/ + include/msh/msh.h + 占位 main.c + --version）
- [x] **doc-sync** — AGENTS.md §10.2 状态从「⏳ 待启动」改成「🟡 骨架完成」+ README.md 同步

## P1 — coreutils 之「最小 IO 集」

- [x] **utils-cat** — `cat` 完整实现（stdin/多文件/- 选项/--help/--version）
- [x] **utils-echo** — `echo` 完整实现（-n/-e/-E、POSIX 退格转义）
- [x] **utils-true** — `true` 空命令（也作 libutils.a 的烟雾测试入口）
- [x] **utils-false** — `false` 空命令
- [x] **utils-yes** — `yes [str]` 无限输出
- [x] **utils-test** — `test`/`[` 命令（POSIX 实现常用表达式）

## P2 — fileutils / coreutils 之「文件操作」

- [x] **utils-cp** — `cp` 文件/目录复制（-r/-p/-f）
- [x] **utils-mv** — `mv` 移动/重命名
- [x] **utils-rm** — `rm` 删除（-r/-f）
- [x] **utils-mkdir** — `mkdir` 创建目录（-p）
- [x] **utils-rmdir** — `rmdir` 删除空目录
- [x] **utils-ln** — `ln` 硬链接/符号链接（-s）
- [x] **utils-touch** — `touch` 创建/更新时间戳
- [x] **utils-ls** — `ls` 列目录（-l/-a/-h 等 GNU 子集）
- [x] **utils-chmod** — `chmod` 改权限（符号 + 八进制）
- [x] **utils-chown** — `chown` 改属主（用户名/数字UID:GID + 递归 + --reference）

## P3 — coreutils 之「文本处理基础」

- [x] **utils-head** — `head` 前 N 行（-n/-c）
- [x] **utils-tail** — `tail` 后 N 行（-n/-c/-f）
- [x] **utils-wc** — `wc` 字节/行/词统计（-l/-w/-c/-m）
- [x] **utils-sort** — `sort` 行排序（-r/-n/-u/-k）
- [x] **utils-uniq** — `uniq` 去重（-c/-d/-u）
- [x] **utils-cut** — `cut` 字段/字符切割（-d/-f/-c）
- [x] **utils-tr** — `tr` 字符替换/删除（-d/-s）
- [x] **utils-tee** — `tee` 双重输出（-a）
- [x] **utils-dd** — `dd` 块复制（bs=/count=/skip=/conv=）

## P4 — diffutils / findutils / 文本处理

- [x] **utils-diff** — `diff` 行对比（POSIX 子集）
- [x] **utils-cmp** — `cmp` 字节对比
- [x] **utils-patch** — `patch` 应用补丁（unified diff + 反向 + -p/-R/--dry-run）
- [x] **utils-find** — `find` 递归查找（-name/-type/-exec/-print 等）
- [x] **utils-xargs** — `xargs` 参数化执行命令
- [x] **utils-grep** — `grep` 模式匹配（POSIX BRE + 可选 ERE）
- [x] **utils-locate** — `locate` 数据库构建+查询（-u/-i/-r/-c/-l/-d）

## P5 — 归档 / 压缩

- [x] **utils-tar** — `tar` 创建/解包/列表（POSIX pax 格式 + gzip 透传 + 长名支持）
- [x] **utils-gzip** — `gzip`/`gunzip` 压缩（DEFLATE 解压全块类型 + stored 压缩 + CRC32 + 系统互操作）
- [x] **utils-mz** — `mz` 原生压缩工具（封装 libmz：.mz 压缩/解压 L1-L9 + .mxa 归档创建/列表/提取/测试）
- [ ] **utils-xz** — `xz`/`unxz` 压缩（如 MeOS 不实现则标记 stalled）
- [ ] **utils-zstd** — `zstd` 压缩（可选）
- [x] **utils-unzip** — `unzip` 解压（PKZIP 格式 + stored/deflate + CRC32 + 列表/测试）

## P6 — msh POSIX sh 核心

- [x] **msh-lex** — 词法：标识符/关键字/操作符/引号/转义/注释
- [x] **msh-parse** — 语法：命令 + 管道 + 列表 + 复合命令
- [x] **msh-exec** — 执行：execvp 集成 + 简单命令
- [x] **msh-var** — 变量：定位参数 / `VAR=val` / `$VAR` `${VAR}` 展开
- [x] **msh-expand** — 展开：glob（`?` `*` `[...]`）/ tilde / 命令替换 `$(...)` / 算术展开 `$((...))`
- [x] **msh-redir** — 重定向：`>` `>>` `<` `2>` `&>`
- [x] **msh-builtin** — 内建：`cd` `echo` `pwd` `export` `unset` `set` `exit` `:` `.` `exec` `read` `eval` `trap` `wait` `jobs`
- [x] **msh-flow** — 控制流：`if`/`then`/`else`/`elif`/`fi` + `case`/`esac` + `for`/`while`/`until`
- [x] **msh-func** — 函数定义与调用

## P7 — msh 交互层

- [x] **msh-prompt** — PS1/PS2 提示符（含基本转义）
- [x] **msh-history** — 历史记录（持久化 ~/.msh_history）
- [x] **msh-lineedit** — 行编辑（vi/emacs 模式 + 字符级操作 + 删除/移动）
- [x] **msh-tab** — Tab 补全（命令 + 路径 + 变量）
- [x] **msh-job** — 作业控制（前台/后台 & + jobs/fg/bg）
- [x] **msh-signal** — 信号处理（Ctrl-C → 终止前台，Ctrl-D → EOF）

## P8 — msh 可选扩展

- [x] **msh-arrays** — bash 风格数组支持（`arr=(a b c)` / `${arr[0]}` / `${arr[@]}` / `${#arr[@]}` / `arr[i]=val`）
- [x] **msh-zsh-plugin** — zsh 风格插件/主题系统（`msh plugin list/load/enable/disable/theme` + 3 内置主题 minimal/colorful/powerline + 文件主题加载）
- [x] **msh-bashcompat** — bash 兼容补充（`[[]]` 条件测试 / `set -e` / `set -o pipefail` 已实现）
- [x] **msh-completion-script** — 补全脚本加载（complete/compgen 内建命令 + 规则注册 + 脚本目录加载）

## P9 — 压缩统一架构（规划中）

> **架构决策**：将所有压缩/解压算法收归 `meuos-compress`（libmz）库，
> `gzip`/`unzip`/`tar` 等工具变为薄壳调用 libmz。

- [x] **utils-mz** — mz 工具封装 libmz（已完成）
- [ ] **mz-deflate-codec** — 在 libmz 中新增 `MZ_CODEC_DEFLATE`（标准 RFC 1951）
- [ ] **mz-pkzip-container** — 在 libmz 中新增 PKZIP 容器格式
- [ ] **gzip-thin-shell** — gzip.c 重构为薄壳（gzip header + libmz DEFLATE）
- [ ] **unzip-thin-shell** — unzip.c 重构为薄壳（PKZIP 解析 + libmz 解压）
- [ ] **tar-mz-support** — tar.c 增加 `-Z` 选项支持 .mz 格式

## P10 — libutils 共享代码重构（✅ 完成）

> **目标**：消除工具间重复代码，提取公共逻辑到 `libutils.a` 共享模块。

- [x] **utils-netinfo** — 提取 ip/ifconfig/route/netstat 4 工具的公共网络信息逻辑到 `libutils/netinfo.c`（/proc/net/dev + ioctl + MAC/IP 格式化 + 路由解析 + CIDR）
- [x] **utils-init** — `utils_init(argc, argv)` 一站式初始化，消除 35+ 工具的手写 version/program_name/--version/--help 样板代码
- [x] **utils-duration** — 提取 `parse_duration`/`parse_duration_ts` 到 `libutils/duration.c`，增强复合时长（1h30m）和冒号格式（1:30:00）。重构 sleep/timeout
- [x] **utils-signame** — 提取信号名表到 `libutils/signame.c`（20→31 信号 + `sig_from_name`/`sig_to_name`/`sig_list_all`）。重构 kill/timeout
- [x] **utils-hex** — 提取 `bytes_to_hex`/`hex_to_bytes` 到 `libutils/hex.c`（支持冒号/连字符分隔）。重构 md5sum/sha256sum
- [x] **utils-hash-bugfix** — 修复 md5sum/sha256sum 3 个预存 bug（MD5 输出字节序交错 + bits padding 污染 + check 模式 sscanf 解析）
- [ ] **utils-digest** — 哈希框架抽象（暂缓，等 SHA-1/SHA-512 等更多算法后再做）

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
| `progname` / `program_name` | argv[0] 提取工具名 |
| `version` / `utils_init` | --version 输出 + 一站式初始化（--version/--help 自动处理） |
| `quote` / `escape` | shell 引用转义 |
| `human_readable` | 字节数 → "1.2K" 格式（ls -h 用） |
| `mbsalign` / `mb_width` | UTF-8 多字节对齐 |
| `netinfo` | 网络信息共享（/proc/net/dev + ioctl + MAC/IP + 路由） |
| `duration` | 时长解析（1h30m / 1:30:00 / 90s → struct timespec） |
| `signame` | 信号名表（sig_from_name/sig_to_name/sig_list_all，31 信号） |
| `hex` | 十六进制转换（bytes_to_hex/hex_to_bytes，支持分隔符） |

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

