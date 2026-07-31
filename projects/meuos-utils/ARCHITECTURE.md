# meuos-utils — 现代核心工具集

> MeuOS Kit 的 coreutils/diffutils/findutils 替代。Phase 7 用户空间首要组件。
>
> **核心哲学**：现代优先 + 兼容层叠加。**核心**按 MeuOS Next 实际需要重塑（不照搬 GNU 风格），**GNU 兼容**作为可选层（`--classic`/`--gnu`），守住核心别被同化。
>
> 不实现「又一个 GNU 工具」。

## 1. 设计原则

1. **§4 强约束**：零 glibc 专有符号 / 零 GNU 代码 / 零 autotools / 系统调用直走 `syscall()` 或 meuos-libc 中性封装
2. **现代优先（核心）**：
   - 每个工具的默认行为是**现代化思考过的**，不是 GNU 的「最古老可移植行为」
   - 颜色、图标、人性化数字、智能默认：默认开，可关
   - 工具间共享一致的 UX 语言（彩色配色、错误格式、退出码语义）
3. **GNU 兼容层（可选）**：
   - 每个工具支持 `--classic` / `--gnu` / `--posix`，启用后**完全**按 GNU 行为
   - 兼容层不污染默认实现；可整体移除 `meuos-utils-gnu-compat` 子目录（未来拆分）
4. **§6.1 三阶段路径**：现代化核心 → 增加 --classic 兼容 → 性能优化
5. **§6.2 不复制 coreutils 源码**：参考 uutils / fd / ripgrep / bat / delta 的设计理念，但用我们自己的 C 代码重写

## 2. 模块化结构

```
meuos-utils/
├── Makefile
├── ARCHITECTURE.md          # 本文件
├── include/meuos/
│   ├── utils.h              # libutils.a 核心 API
│   ├── color.h              # 24-bit ANSI 颜色
│   ├── progress.h           # 进度条 + ETA
│   ├── icons.h              # 文件类型图标（Nerd/ASCII 退化）
│   ├── table.h              # 自适应列宽表格
│   ├── json.h               # JSON 美化输出
│   ├── syntax.h             # 轻量语法高亮
│   ├── config.h             # YAML 配置加载（简化解析）
│   └── hash.h               # 简单哈希表（gitignore 用）
├── src/
│   ├── libutils/            # 静态库 libutils.a
│   │   ├── color.c          # ANSI 颜色
│   │   ├── progress.c       # TTY 进度条
│   │   ├── icons.c          # 图标退化
│   │   ├── table.c          # 表格渲染
│   │   ├── json.c           # JSON pretty
│   │   ├── syntax.c         # 轻量着色
│   │   ├── config.c         # YAML 加载
│   │   ├── hash.c           # 哈希表
│   │   ├── human.c          # 字节数人性化
│   │   ├── pathname.c       # 安全 basename/dirname
│   │   ├── getopt.c         # GNU 长选项子集
│   │   ├── xmalloc.c        # OOM 分配器
│   │   └── version.c        # 版本信息
│   ├── coreutils/           # 第一波核心（默认现代优先）
│   │   ├── ls.c             # 现代：彩色+图标+树
│   │   ├── tree.c           # tree（ls --tree 或独立）
│   │   ├── cat.c            # 现代：bat-lite
│   │   ├── cp.c             # 现代：进度+原子
│   │   ├── mv.c             # 现代：rename 优先
│   │   ├── rm.c             # 现代：trash 安全层
│   │   ├── touch.c
│   │   ├── mkdir.c
│   │   ├── rmdir.c
│   │   ├── ln.c
│   │   ├── stat.c           # 现代 stat
│   │   └── chmod.c
│   ├── text/                # 文本处理
│   │   ├── head.c
│   │   ├── tail.c           # 现代：-f 异步监控
│   │   ├── wc.c
│   │   ├── sort.c           # 现代：智能数字/版本排序
│   │   ├── uniq.c
│   │   ├── cut.c
│   │   ├── tr.c
│   │   ├── tee.c
│   │   ├── grep.c           # ripgrep-lite
│   │   ├── sed.c            # POSIX sed + GNU 扩展（可选）
│   │   ├── awk.c            # POSIX awk 子集
│   │   └── diff.c           # delta-lite
│   ├── findutils/
│   │   ├── find.c           # fd 风格
│   │   ├── xargs.c
│   │   └── locate.c         # 后续：构建/查询数据库
│   ├── archive/
│   │   └── tar.c            # pax 格式 + gzip 选项
│   └── compress/
│       ├── gzip.c           # 基于 mz 库
│       └── xz.c
├── test/
│   ├── smoke.sh             # 烟雾测试集
│   ├── posix/               # POSIX 兼容测试
│   └── modern/              # 现代 UX 行为样板
└── examples/
    ├── meou.yaml.example    # 默认配置样板
    └── theme.yaml.example   # 主题样板
```

## 3. 现代化 UX 约定（核心规范）

每个现代工具默认遵循：

| 维度 | 默认现代 | 关闭方法 |
|------|---------|----------|
| 颜色 | 自动（除非 NO_COLOR 或非 tty） | `--no-color` 或 `--classic` |
| 图标 | 优先 Nerd Font，回退 ASCII | `--no-icons` 或 `--classic` |
| Human-readable 字节数 | 默认开 | `--bytes` 或 `--classic` |
| Git 集成 | 显示文件状态（如可用） | `--no-git` 或 `--classic` |
| 输出格式 | 表格默认 | `--json`/`--plain` 等 |
| 错误格式 | `<tool>: <context>: <msg>` | 强制 `--classic` 时改 `tool: msg` |

### 退出码规范

| 退出码 | 含义 |
|--------|------|
| 0 | 成功 |
| 1 | 一般错误（部分文件失败等） |
| 2 | 用法错误（参数错） |
| 64–78 | 遵循 BSD sysexits.h（EX_USAGE=64, EX_DATAERR=65, ...） |
| 126 | 命令找到了但不可执行 |
| 127 | 命令未找到 |

## 4. 默认现代化工具一览（核心）

### ls 现代核心
- 默认彩色 + 文件类型图标 + human-readable
- 默认 `-l` + `-G`（无 group）+ 目录优先 + 按名称排序
- 长格式智能宽度：自动适配终端宽度
- 文件大小：根据大小切换人类可读（K/M/G）
- Git 状态标记：未跟踪 / 已修改 / 已暂存（如 `git` 在 PATH）
- JSON 输出：`--json` 输出结构化列表
- 树视图：`--tree [DEPTH]` 或 `tree` 独立命令
- **GNU 兼容**：`--classic` 切到 POSIX/BSD ls 风格

### cat 现代核心（bat-lite）
- 默认行号 + 语法检测着色（基于扩展名/token，不做完整解析）
- JSON 文件自动 pretty-print
- Markdown/YAML/Shell 轻量高亮
- 多文件：分隔线 + 文件名头
- 智能 PAGER：当 stdout 非 tty 且内容长时启用 less
- **GNU 兼容**：`--classic` 纯 cat

### find 现代核心（fd 风格）
- 默认 regex 搜索（不是 -name only）
- 默认跳过 `.git`、`.hg`、`.svn`、`node_modules`、目标文件/构建产物
- 尊重 `.gitignore`（内置解析简化版）
- 彩色高亮匹配
- 单行输出：`<path>` 或 `<path>:<match-line>`
- 默认递归
- **GNU 兼容**：`--classic` 切到 POSIX find

### grep 现代核心（ripgrep-lite）
- 默认递归
- 默认跳过 `.gitignore` 覆盖文件
- 彩色高亮匹配（如 stdout is tty）
- 智能大小写（默认敏感）
- 输出格式：`<file>:<line>:<content>`
- 多文件 + 单文件自适应
- **GNU 兼容**：`--classic` 切到 POSIX grep

### diff 现代核心（delta-lite）
- 默认 unified + 彩色 + 单词级高亮
- 文件识别
- 双栏或单栏：自动按终端宽度选择
- **GNU 兼容**：`--classic` 纯 text unified diff

### cp/mv/rm 现代核心
- cp：默认进度条 + 原子替换（先写 tmp 再 rename）+ preserve 模式可选
- mv：rename(2) 优先，跨 fs 走 cp+rm 退化
- rm：默认 --trash 到 `~/.local/share/Trash/files/`（BSD rm 风格 -i 提示保留），`-f` 强制硬删除
- **GNU 兼容**：`--classic` 不带 trash

## 5. GNU 兼容策略

- 每个现代工具支持 `--classic`/`--gnu`/`--posix` 切换
- 兼容模式下：禁掉所有现代特性（颜色/图标/智能默认）
- 兼容模式与 GNU/POSIX 行为差异通过 `test/posix/` 测试集保证
- **不**做完整 GNU 100% 兼容：只覆盖 MeuOS Next 实际使用的子集
- 不兼容的 GNU 选项返回明确的 `<tool>: option X not supported in --classic` 错误（不静默忽略）

## 6. libutils.a 现代化模块

| 模块 | 头文件 | 功能 |
|------|--------|------|
| 颜色 | `color.h` | 24-bit ANSI 颜色 + 检测 NO_COLOR 环境 |
| 进度 | `progress.h` | TTY 进度条 + ETA + 字节/单位格式 |
| 图标 | `icons.h` | 文件类型→图标（Nerd Font + ASCII fallback） |
| 表格 | `table.h` | 自动列宽 + 对齐 + 着色 |
| JSON | `json.h` | 简化 JSON 解析 + pretty-print 输出 |
| 语法 | `syntax.h` | 轻量 token 着色（基于扩展名/token，无正则） |
| 配置 | `config.h` | YAML 简化解析（自己实现或调 meow libm4） |
| 哈希 | `hash.h` | 简单哈希表（gitignore 跳过、ignore 集合） |

## 7. 编译路径

### 宿主路径（默认）
```sh
make -C projects/meuos-utils           # cc + libutils.a + libc 系统库
make -C projects/meuos-utils check     # 烟雾 + POSIX sh 测试 + 现代 UX 样板
```

### sysroot 路径（mcc + meuos-libc）
```sh
make -C projects/meuos-utils CC=/path/to/mcc \
    CFLAGS='--specs=meuos --sysroot=/path/to/sysroot/x86_64 --nostdlib -O2'
```

### 安装
```sh
make -C projects/meuos-utils install DESTDIR=/tmp/sysroot PREFIX=/usr
# /tmp/sysroot/usr/bin/{ls,cat,find,grep,diff,cp,mv,rm,tree,wc,...}
# /tmp/sysroot/usr/lib/libutils.a
# /tmp/sysroot/usr/share/meuos-utils/{icons,colors}.yaml  # 默认配置
```

## 8. 测试策略

| 层 | 工具 | 检查 |
|----|------|------|
| **烟雾** | `make check` | 每个工具 `--help`/`--version` + 一个最小行为 |
| **POSIX 兼容** | `test/posix/` | `--classic` 模式下与 POSIX/GNU 行为对照 |
| **现代 UX** | `test/modern/` | 默认输出的彩色快照（端末检测关闭时跳过） |
| **回归** | `test/regress/` | 之前发现过的 bug 修复后不复发 |

## 9. 与其他组件的依赖关系

```
meuos-utils
  ↓ 链接
libc-meuos.a + libutils.a (本项目)
  ↓ 不依赖其他
meow / msh / 工具链 / sysroot
```

## 10. 路线图

### Phase 7A — 骨架 + 现代核心（当前）
- ✅ 骨架阶段烟雾测试 5 工具通过
- 🟡 libutils.a 现代化（color/progress/icons/table/json/config/syntax/hash）
- 🟡 现代 ls/cat/find/grep/diff/cp/mv/rm + tree
- 🟡 剩余 coreutils（wc/head/tail/sort/uniq/tr/cut/tee）
- 🟡 GNU `--classic` 兼容层

### Phase 7B — 文本处理深水区
- sed（POSIX sed，复杂）
- awk（POSIX awk 子集，复杂）
- locate + 数据库构建
- tar + 压缩工具
- 用户请求的网络/高级工具

### Phase 7C — 优化
- 并行化（sort -jN/grep -jN）
- mmap 大文件处理
- SIMD 加速（grep 字节搜索等）

## 11. 实施笔记

### 11.1 为什么先做现代核心

如果先实现 GNU 兼容版再改现代化，会不可避免地带上 GNU 的历史包袱（POSIX 边界、GNU extension 命名冲突）。我们直接做现代化核心，**GNU 兼容**是后续加的兼容层，**不会影响核心实现**。

### 11.2 关于"不模仿 GNU"的几条具体规则

1. **不抄 GNU 工具名**：cp/mv/rm 等核心命令保留（POSIX 必需），但 GUI/UX 行为按 MeuOS 风格
2. **不抄 GNU 选项名**：使用现代常见的命名（`--human-readable` vs `-h`、`--no-color` vs `--color=never`）
3. **不抄 GNU 错误格式**：`ls: cannot access 'foo': No such file or directory` → `<tool>: foo (ERRNO=2): not found` 或类似现代化风格
4. **不抄 GNU 默认行为**：如 sort 默认行为按 locale（GNU 默认按 LC_COLLATE）vs 我方默认 UTF-8 字节序 + 智能数字/版本排序
5. **不抄 GNU 长短选项**：发现 GNU 不合理的设计就重做（如 GNU `rm` 缺 trash，是公认的落后设计）
