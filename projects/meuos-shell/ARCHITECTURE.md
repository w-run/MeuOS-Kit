# meuos-shell (msh) — MeuOS Shell

> MeuOS Kit 的 POSIX Shell 终端。Phase 7 用户空间首要组件（与 `meuos-utils` 并列）。
> 目标：完整 POSIX.1-2008 Shell 命令语言 + 可选 bash 兼容 + 可选 zsh 插件/主题。
>
> 项目规约见 `../../AGENTS.md` §2.6 + §4 + §6.
> 任务追踪见 `../../.issues/INDEX.md`.

## 1. 定位

`msh` 是 MeuOS Next 用户空间的 Shell 解释器，依赖：
- `mcc`（编译器）— Phase 1 完成
- `meuos-libc`（标准 C 库）— Phase 2 完成
- `meuos-toolchain`（as/ld/ar 等）— Phase 5 完成
- `meuos-utils`（cat/echo/grep 等外部命令）— Phase 7 同期

`msh` 可**调用** meuos-utils 的工具作为外部命令（PATH 解析）。同时 `msh` 也是 meow 构建系统的可选执行引擎（Phase 7 后阶段会替代宿主 `/bin/sh`）。

## 2. 设计原则

1. **§4 强约束**：零 glibc 专有符号 / 零 GNU 代码 / 零 bash 源码复制 / 零 autotools / 系统调用直走 `syscall()` 或 meuos-libc 中性封装
2. **§6.1 三阶段路径**：标准化可用（POSIX sh）→ 有利特性（bash 兼容 + zsh 插件）→ 性能优化
3. **§6.2 不复制 bash/dash 源码**：参考 dash（POSIX sh 黄金参考）的整体架构和 zsh 插件系统设计，但用我们自己的 C 代码重写每个模块
4. **模块化分层**：核心引擎（lex/parse/exec/builtin/var）+ 可选 bash 兼容层 + 可选 zsh 插件/主题
5. **POSIX 优先**：先实现 POSIX.1-2008 Shell 命令语言完整子集，bash/zsh 兼容作为额外层
6. **可重现构建**（§4）：无时间戳、无硬编码路径

## 3. 目录结构

```
meuos-shell/
├── Makefile                 # 简单 Makefile（§4 禁止 autotools/cmake）
├── ARCHITECTURE.md          # 本文件
├── include/
│   └── msh/
│       ├── msh.h            # 公共 API（version/program_name/die）
│       ├── lex.h            # 词法分析器接口（P6 实装）
│       ├── parse.h          # 语法分析器接口（P6 实装）
│       ├── exec.h           # 命令执行器接口（P6 实装）
│       ├── builtin.h        # 内建命令表接口（P6 实装）
│       ├── var.h            # 变量管理接口（P6 实装）
│       ├── expand.h         # 展开（glob/tilde/参数/命令替换）（P6 实装）
│       ├── redir.h          # 重定向接口（P6 实装）
│       ├── job.h            # 作业控制接口（P7 实装）
│       └── history.h        # 历史记录接口（P7 实装）
├── src/
│   ├── main/main.c          # argv 解析 + 模式分发（交互/脚本/单命令）
│   ├── lex/                 # 词法（P6 占位）
│   ├── parse/               # 语法（P6 占位）
│   ├── exec/                # 执行（P6 占位）
│   ├── builtin/             # 内建（P6 占位）
│   └── var/                 # 变量（P6 占位）
├── test/
│   ├── posix/               # POSIX sh 烟雾测试集
│   │   └── smoke.sh
│   └── interactive/         # 交互层测试（QEMU 环境手测，P7 后）
└── .todo/                   # 子任务设计文档
```

## 4. 当前能力（P7 中期）

| 能力 | 状态 | 说明 |
|------|:----:|------|
| `--version` / `--help` | ✅ | 版本与 usage 输出 |
| `-c "cmd"` | ✅ | 单命令模式：parse + eval |
| 脚本模式 `msh script.sh` | ✅ | 完整解析执行 |
| 注释行 (`#`) / 空行 | ✅ | 词法层处理 |
| 交互模式（REPL） | ✅ | 行编辑 + 历史 + 作业回收 |
| 内建命令 | ✅ | cd/export/unset/set/exit/echo/pwd/read/type/exec/jobs/fg/bg/wait 等 |
| 管道 (`\|`) | ✅ | N 级管道 |
| 重定向 (`>` `<` `>>` `>&2` `<&N`) | ✅ | 含 fd 复制 |
| 变量 (`$VAR` `${VAR}` 修饰符) | ✅ | `:-` `:=` `:+` `:?` `#` `##` `%` `%%` `/` 替换 `${#VAR}` |
| 控制流 | ✅ | if/elif/else/for/while/until/case |
| 函数定义 | ✅ | `name() {}` 与 `function name {}`，位置参数 `$1..$n` `$@` `$#` |
| 命令替换 `$(...)` / 反引号 | ✅ | fork+pipe，支持嵌套（递归函数验证通过） |
| 算术 `$((...))` | ✅ | 递归下降求值器（+ - * / % 比较 逻辑） |
| Glob (`*.c`) / tilde (`~`) | ✅ | libc glob/fnmatch |
| 后台作业 `&` | ✅ | jobs/fg/bg/wait + SIGCHLD 回收 |
| 历史/行编辑 | ✅ | 裸 termios，`~/.msh_history`（限 1MB） |
| YAML 配置 | ✅ | `~/.config/msh/config.yaml`（ps1/aliases/env/features） |
| 兼容 3 路递进 | ✅ | `--classic` > `MSH_CLASSIC` env > yaml features |
| Tab 补全 | ❌ | P7C |
| here-doc `<<EOF` | ❌ | P7C |
| trap 内建 | ❌ | P7C |
| bash 数组/`[[]]`/`source` | ❌ | P8（可选） |
| zsh 插件/主题 | ❌ | P8（可选） |

### 骨架运作原理

骨架版本采用"行级 fork/execvp" 模型：

```
msh script.sh
  → fopen + getline 按行读
    → 跳过空行/注释行
      → fork()
        → child: execvp(argv[0], argv)
        → parent: waitpid() 取退出码
```

`run_simple()` 用空白分割每行得到 argv（不处理引号/转义），调用 execvp。  
execvp 自己处理 PATH 解析（man execvp：`如果 file 含斜杠视为路径；否则搜索 PATH`）。

### 骨架阶段的边界

骨架**不支持**任何 shell 语法（变量/管道/重定向/引号）。这意味着：

- 不能用骨架 msh 执行真实 shell 脚本
- 不能用骨架 msh 替代宿主 /bin/sh
- 但可以验证：argv 解析 + fork+execvp + interactive I/O 的基础管线

后续 P6 阶段逐步引入：
1. 词法（lex）— 单引号/双引号/转义/变量前缀 `$/` 识别
2. 语法（parse）— 简单命令 + 管道 + 重定向 + 列表结构
3. 变量（var）— `$VAR`/`${VAR}` 展开
4. 内建（builtin）— `exit`/`cd`/`pwd`/`export`/`set` 等
5. 控制流 — `if`/`for`/`while`/`case`/`function`

## 5. 路线图

### Phase 7A — 骨架 + 基本 IO（当前）

**已完成**：
- ✅ 入口 + argv 解析 + `--version`/`--help`
- ✅ `-c "command"` 执行单条外部命令
- ✅ 脚本模式逐行执行
- ✅ 交互模式 REPL（getline-based，无历史）
- ✅ 模块化目录结构 + 公共 API 头文件
- ✅ 烟雾测试 3 项全过

### Phase 7B — POSIX sh 核心（P6 任务集）

按模块逐个实现：

1. **msh-lex**（1-2 周）— 词法：标识符/关键字/操作符/引号/转义/注释
2. **msh-parse**（2-3 周）— 语法：命令 + 管道 + 列表 + 复合命令
3. **msh-exec**（1 周）— 执行：内建 vs 外部命令派发
4. **msh-var**（1 周）— 变量：定位参数 + `$VAR`/`${VAR}` 展开
5. **msh-expand**（1-2 周）— 展开：glob/tilde/命令替换/算术展开
6. **msh-redir**（1 周）— 重定向：`>` `>>` `<` `2>` `&>`
7. **msh-builtin**（1-2 周）— 内建命令表
8. **msh-flow**（2-3 周）— 控制流：`if`/`case`/`for`/`while`/`until`/`function`
9. **msh-posix-test**（持续）— POSIX sh 烟雾测试集扩展

### Phase 7C — 交互层（P7 任务集）

1. **msh-prompt** — PS1/PS2 提示符（基本转义）
2. **msh-history** — 历史记录（持久化 `~/.msh_history`）
3. **msh-lineedit** — 行编辑（vi/emacs 模式 + 字符级操作）
4. **msh-tab** — Tab 补全（命令 + 路径 + 变量）
5. **msh-job** — 作业控制（前台/后台 `&` + `jobs`/`fg`/`bg`）
6. **msh-signal** — 信号处理（Ctrl-C → 终止前台，Ctrl-D → EOF）

### Phase 7D — 扩展（P8 任务集）

1. **msh-bashcompat** — bash 兼容层（数组 `${arr[@]}` / `[[]]` / `source` / `set -e`）
2. **msh-zsh-plugin** — zsh 风格插件/主题（参考 zsh 的 oh-my-zsh 生态）
3. **msh-arrays** — bash 风格数组支持
4. **msh-completion-script** — 补全脚本加载（`compctl`/`compgen`）

## 6. 编译路径

### 宿主路径（默认）
```sh
make -C projects/meuos-shell           # cc + libc 系统库
make -C projects/meuos-shell check     # 跑内置烟雾测试
```

### sysroot 路径（mcc + meuos-libc）
```sh
make -C projects/meuos-shell CC=/path/to/mcc \
    CFLAGS='--specs=meuos --sysroot=/path/to/sysroot/x86_64 --nostdlib -O2'
```

### 安装
```sh
make -C projects/meuos-shell install DESTDIR=/tmp/sysroot PREFIX=/usr
# 输出：
#   /tmp/sysroot/usr/bin/msh
```

## 7. 测试策略

| 层 | 工具 | 检查 |
|----|------|------|
| **烟雾** | `make check` | `--version`/`-c`/`script.sh` 三模式 |
| **POSIX** | `test/posix/smoke.sh` | POSIX sh 规范基本场景（脚本语言测试） |
| **shellcheck** | `shellcheck test/posix/smoke.sh` | 静态检查脚本无语法错误 |
| **行为** | 与 dash 对比 | `diff <(./msh -c cmd) <(dash -c cmd)` |

POSIX sh 测试集（`test/posix/smoke.sh`）将在 P6 阶段编写，覆盖：
- 简单命令 + 退出码
- 管道 + 重定向
- 变量 + 参数展开 + 命令替换
- 控制流（if/while/for/case）
- 函数定义 + 调用
- 信号处理基础

## 8. 与其他组件的依赖关系

```
meuos-shell (msh)
  ↓ 链接
libc-meuos.a (libc) — mcc 编译路径
  ↓ 运行时调用
meuos-utils 工具 — 外部命令 (cat echo grep 等)
  ↓ 备选
GNU coreutils — fall back /bin/sh 调用（MeuOS 中不再）
```

msh 自身**不**依赖 meow，但 Phase 7 后阶段会用 msh 替代 meow 配方中的 `/bin/sh` 调用（`run(?)` 等），形成完整自举闭环。

## 9. 实施笔记

### 9.1 为什么骨架阶段没用 lex/parse 模块

骨架只用 `split_args()` 一个函数按空白分割 argv。这是为了：
- 验证 argv 解析 + fork/execvp 链路
- 不被 lex/parse 复杂性阻塞基础管线
- P6 阶段才用真正的词法/语法/解释器替代

骨架不是设计简化，是已知占位实现。

### 9.2 关于"bash"兼容范围的诚实声明

AGENTS.md §2.6 提到"可选 bash 兼容"，但完整 bash 兼容性是个无底洞。  
设计原则是：**POSIX sh 子集 100% 实现；bash 兼容仅实现 MeuOS Next 实际需要的子集**。
具体哪些 bash 特性值得支持，待 P8 阶段根据 meow 构建 / 用户社区反馈确定。

### 9.3 多 shell 不与 dash 冲突

MeuOS Next 中 msh 是默认 /bin/sh。但**不**禁止 dash/bash 共存——若某些场景有特殊需要，可保留 dash 作为可选依赖。  
msh 的目标是取代 dash 在"脚本运行"的角色；交互场景的目标用户主要是开发者和运维，不需要 oh-my-zsh 那种花哨体验。
