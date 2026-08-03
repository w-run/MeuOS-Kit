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
│   │   ├── version.c        # 版本 + utils_init/utils_usage/utils_die_usage
│   │   ├── classic.c        # --classic 模式
│   │   ├── netinfo.c        # 网络信息共享 (/proc/net/dev + ioctl + 路由)
│   │   ├── duration.c       # 时长解析 (1h30m / 1:30:00 / 90s)
│   │   ├── signame.c        # 信号名表 (sig_from_name/sig_to_name/sig_list_all)
│   │   └── hex.c            # 十六进制转换 (bytes_to_hex/hex_to_bytes)
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
| **netinfo** | `netinfo.h` | 网络信息共享：/proc/net/dev 解析 + ioctl 调用 + MAC/IP 格式化 + 路由解析 |
| **duration** | `utils.h` | 时长解析：复合格式 `1h30m` / 冒号格式 `1:30:00` / 简单 `90s` → `struct timespec` |
| **signame** | `utils.h` | 信号名表：`sig_from_name()` / `sig_to_name()` / `sig_list_all()`（31 信号） |
| **hex** | `utils.h` | 十六进制转换：`bytes_to_hex()` / `hex_to_bytes()`（支持冒号/连字符分隔） |
| **utils_init** | `utils.h` | 一站式初始化：`--version` / `--help` 自动处理 + `program_name` 设置 + `argi` 返回 |

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

### Phase 7A — 骨架 + 现代核心（✅ 完成）
- ✅ 骨架阶段烟雾测试 5 工具通过
- ✅ libutils.a 现代化（xmalloc/getopt/progname/version/human/pathname）
- ✅ 现代 ls/cat/find/grep/diff/cp/mv/rm + tree（均含 `--classic` 兼容）
- ✅ 剩余 coreutils（wc/head/tail/sort/uniq/tr/cut/tee/seq/printf/env/dd/stat/test/cmp/chmod/echo/true/false/yes/mkdir/rmdir/ln/touch/xargs）
- ✅ sed（POSIX 子集：s/d/p/a/i/c/q/y/=/w/r + 地址匹配）
- ✅ GNU `--classic` 兼容层（ls/cat/find/grep/diff/cp/mv/rm 等）

### Phase 7B — 文本处理深水区（✅ 完成）
- ✅ sed（POSIX sed 子集，已完成）
- ✅ awk（POSIX awk 子集：BEGIN/END/模式-动作/字段/正则/控制流/数组/gsub/sub/split/printf/重定向/管道/隐式拼接）
- ✅ locate + 数据库构建（子串/正则/大小写/计数/限制）
- ✅ tar（pax 格式创建/解包/列表 + gzip 透传 + 长名支持）
- ✅ gzip（DEFLATE 解压全块类型 + stored 压缩 + CRC32 + 系统 gzip 互操作）
- ✅ patch（unified diff 应用 + 反向 + -p/-R/--dry-run）
- ✅ diff 扩展（unified `@@ ... @@` + context `***`/`---` 格式 + `-c`/`-u`/`-C N`/`-U N`）

### Phase 7C — 优化
- ✅ **mz 工具集成 libmz** — 封装 `meuos-compress` 库，提供 `.mz` 压缩/解压 + `.mxa` 归档创建/列表/提取/测试
- ✅ **目录重构** — 按 core/text/file/arch/sys/net 分类重构 src/utils/ 目录（73 文件迁移 + Makefile VPATH 适配）
- ✅ **网络工具集** — ifconfig/route/netstat/ping/curl/wget（6 个网络工具）
- ⏳ 并行化（sort -jN/grep -jN）
- ⏳ mmap 大文件处理
- ⏳ SIMD 加速（grep 字节搜索等）

### Phase 7E — Shell-Utils 联动（✅ 完成）
- ✅ **高性能内建** — printf/test/[ /sleep/seq 作为 msh 内建，避免 fork+exec
- ✅ **libutils.a 共享** — msh 链接 libutils.a，共享 xmalloc/color/getopt/version
- ✅ **105 项回归测试** — 含 Shell-Utils 联动内建测试

### Phase 7D — 压缩统一架构（✅ 完成）
> **架构决策**：将所有压缩/解压算法收归 `meuos-compress` 库，
> `gzip`/`unzip`/`tar` 等工具变为薄壳调用 libmz。

- ✅ 在 `meuos-compress` 中新增 DEFLATE codec（`MZ_CODEC_DEFLATE`）— RFC 1951 stored blocks 压缩 + 全块类型解压（stored/fixed Huffman/dynamic Huffman）
- ✅ 在 `meuos-compress` 中新增 Gzip 容器格式（`mz_gzip.c`）— RFC 1952 header/footer + CRC32 校验 + 与系统 gzip 互操作
- ✅ 在 `meuos-compress` 中新增 PKZIP 读取器（`mz_zip.c`）— EOCD 扫描 + 中央目录解析 + stored/deflate 提取 + CRC32 校验
- ✅ gzip.c 重构为薄壳（311 行 → 311 行）：调用 `mz_gzip_compress`/`mz_gzip_decompress`
- ✅ unzip.c 重构为薄壳（877 行 → 299 行）：调用 `mz_zip_reader_*` API
- ✅ tar.c 增加 `-Z` 选项：使用 `mz_gzip_compress`/`mz_gzip_decompress`，输出标准 gzip 格式
- ✅ LZ77 v2 token 格式修复：escape literal 前缀从 0x81 改为 0xFF，消除与 match token b0 的冲突

### Phase 7F — libutils 共享代码重构（✅ 完成）

> **目标**：消除工具间重复代码，提取公共逻辑到 `libutils.a` 共享模块。

- ✅ **netinfo 共享模块** — 提取 ip/ifconfig/route/netstat 4 工具的公共网络信息逻辑（/proc/net/dev 解析 + ioctl 调用 + MAC/IP 格式化 + 路由解析 + CIDR 计算）到 `libutils/netinfo.c`
- ✅ **utils_init 一站式初始化** — 消除 35+ 工具的手写 version/program_name 样板代码。`utils_init(argc, argv)` 自动处理 `--version`/`--help`、设置 `program_name`、返回 `argi`
- ✅ **parse_duration 时长解析** — 提取到 `libutils/duration.c`，增强复合时长（`1h30m`）和冒号格式（`1:30:00`）。重构 sleep/timeout
- ✅ **signame 信号名表** — 提取到 `libutils/signame.c`，信号表 20→31 + `sig_from_name()`/`sig_to_name()`/`sig_list_all()`。重构 kill/timeout
- ✅ **hex 十六进制转换** — 提取到 `libutils/hex.c`，`bytes_to_hex()`/`hex_to_bytes()`（支持冒号/连字符分隔）。重构 md5sum/sha256sum
- ✅ **md5sum/sha256sum 哈希算法 bug 修复** — 修复 3 个预存 bug：MD5 输出字节序交错 + bits padding 污染 + check 模式 sscanf 解析

## 11. 当前能力矩阵

| 工具 | 状态 | 已实现特性 | 待实现 |
|------|------|-----------|--------|
| ls | ✅ | 彩色+图标+human+树视图+`--classic`+`--json` | — |
| cat | ✅ | 行号+语法着色+JSON pretty+`--classic` | — |
| find | ✅ | regex+跳过 VCS+`--classic` | — |
| grep | ✅ | 递归+着色+`--classic` | — |
| diff | ✅ | unified+context格式+着色+`-c`/`-u`/`-C N`/`-U N` | — |
| cp | ✅ | 进度+原子+`--classic` | — |
| mv | ✅ | rename 优先+`--classic` | — |
| rm | ✅ | trash 安全层+`--classic` | — |
| tree | ✅ | 彩色+深度控制+`--classic` | — |
| wc | ✅ | 字/行/字节/字符统计 | — |
| head/tail | ✅ | 字节/行模式 | tail -f |
| sort | ✅ | 智能数字/版本+`--classic` | — |
| uniq | ✅ | 计数+重复/唯一筛选 | — |
| cut | ✅ | 字段/字节/字符+delimiter | — |
| tr | ✅ | 字符映射+删除+压缩 | — |
| tee | ✅ | 多文件输出+追加模式 | — |
| seq | ✅ | 数字序列+步进+格式化 | — |
| printf | ✅ | 格式化输出+转义 | — |
| env | ✅ | 环境变量操作+清环境运行 | — |
| dd | ✅ | 块级复制+转换+进度 | — |
| stat | ✅ | 文件信息+`--classic`+`--json` | — |
| test | ✅ | POSIX `[` 测试+文件/字符串/整数 | — |
| cmp | ✅ | 字节比较+静默+偏移 | — |
| chmod | ✅ | 符号/八进制权限 | — |
| echo | ✅ | 转义控制+`-n` | — |
| true/false/yes | ✅ | 标准退出码 | — |
| mkdir/rmdir | ✅ | 递归创建+权限 | — |
| ln | ✅ | 硬链接+软链接+强制 | — |
| touch | ✅ | 创建/更新+`-c` | — |
| xargs | ✅ | 参数分批+替换+并行占位 | — |
| sed | ✅ | s/d/p/a/i/c/q/y/=/w/r+地址 | 多行模式 |
| awk | ✅ | BEGIN/END/模式-动作/字段/NR/NF/FS/OFS/正则/控制流/数组/gsub/sub/split/printf/重定向/管道 | 变量赋值gsub |
| locate | ✅ | 数据库构建+子串/正则/大小写/计数/限制 | — |
| tar | ✅ | pax格式创建/解包/列表+gzip透传+长名 | xz/zstd |
| gzip | ✅ | DEFLATE解压(全块类型)+stored压缩+CRC32+互操作 | LZ77压缩 |
| patch | ✅ | unified diff应用+反向+-p/-R/--dry-run | context格式 |
| chown | ✅ | 用户名/数字UID:GID+递归+--reference | — |
| unzip | ✅ | PKZIP解压+stored/deflate+CRC32+列表+测试 | zip创建 |
| **mz** | ✅ | libmz封装：.mz压缩/解压(L1-L9)+.mxa归档创建/列表/提取/测试 | — |
| **ifconfig** | ✅ | 接口信息+统计+getifaddrs+MAC地址 | — |
| **route** | ✅ | 路由表查看(/proc/net/route) | — |
| **netstat** | ✅ | TCP/UDP/Unix套接字+路由+接口+统计 | — |
| **ping** | ✅ | ICMP echo+RTT统计+超时 | IPv6 |
| **curl** | ✅ | HTTP/1.1 GET/POST+重定向+头+输出 | HTTPS/TLS |
| **wget** | ✅ | HTTP下载+断点续传+重试+进度 | HTTPS/TLS |
| **ip** | ✅ | addr/link/route/neigh 子命令 | netlink 协议 |
| **nslookup** | ✅ | DNS A/AAAA/MX/TXT/CNAME/NS/PTR 查询 | — |
| **telnet** | ✅ | IAC 协议客户端+终端协商 | — |
| **md5sum** | ✅ | RFC 1321 MD5+check 模式+stdin | — |
| **sha256sum** | ✅ | FIPS 180-4 SHA-256+check 模式+stdin | — |
| **kill** | ✅ | 信号发送+`-l` 列出+名称/编号 | — |
| **sleep** | ✅ | 复合时长(1h30m)+多参数 | — |
| **timeout** | ✅ | 命令超时终止+信号选择 | — |
| **date** | ✅ | 时间格式化+设置+UTC | — |
| **hostname** | ✅ | 主机名查看+设置 | — |
| **whoami** | ✅ | 当前用户名 | — |
| **id** | ✅ | UID/GID/组信息 | — |
| **uname** | ✅ | 系统信息(-a/-m/-r) | — |
| **which** | ✅ | 命令路径查找 | — |

## 12. 实施笔记

### 12.1 为什么先做现代核心

如果先实现 GNU 兼容版再改现代化，会不可避免地带上 GNU 的历史包袱（POSIX 边界、GNU extension 命名冲突）。我们直接做现代化核心，**GNU 兼容**是后续加的兼容层，**不会影响核心实现**。

### 12.2 关于"不模仿 GNU"的几条具体规则

1. **不抄 GNU 工具名**：cp/mv/rm 等核心命令保留（POSIX 必需），但 GUI/UX 行为按 MeuOS 风格
2. **不抄 GNU 选项名**：使用现代常见的命名（`--human-readable` vs `-h`、`--no-color` vs `--color=never`）
3. **不抄 GNU 错误格式**：`ls: cannot access 'foo': No such file or directory` → `<tool>: foo (ERRNO=2): not found` 或类似现代化风格
4. **不抄 GNU 默认行为**：如 sort 默认行为按 locale（GNU 默认按 LC_COLLATE）vs 我方默认 UTF-8 字节序 + 智能数字/版本排序
5. **不抄 GNU 长短选项**：发现 GNU 不合理的设计就重做（如 GNU `rm` 缺 trash，是公认的落后设计）

### 12.3 压缩外包架构决策（2026-08-02）

**决策**：所有压缩/解压算法统一收归 `meuos-compress`（libmz）库，`gzip`/`unzip`/`tar` 等工具不再各自实现压缩算法，变为薄壳调用 libmz。

**理由**：
1. 消除重复代码：gzip.c 和 unzip.c 各自携带 ~250 行 DEFLATE 解压实现
2. 统一算法入口：所有压缩/解压通过 `mz_compress()` / `mz_decompress()` 统一 API
3. 可维护性：算法升级只需改 libmz 一处

**实施路径**：
1. 在 libmz 中新增 `MZ_CODEC_DEFLATE`（标准 RFC 1951），使 gzip 能调用 libmz 处理 .gz 文件
2. 在 libmz 中新增 PKZIP 容器格式（类似 mxa），使 unzip 能调用 mxa API
3. gzip.c 重构：仅保留 gzip header/footer 封装，DEFLATE 部分调用 libmz
4. unzip.c 重构：仅保留 PKZIP 中央目录解析，解压部分调用 libmz
5. tar.c 增加 `-Z` 选项支持 .mz 格式

**当前状态**：mz 工具已完成（Phase 7C），其余为后续实现。

### 12.4 libutils 共享代码重构与哈希算法 bug 修复（2026-08-03）

**背景**：在实现 ip/nslookup/telnet 网络工具后，发现工具间存在大量重复代码（version/help 样板、网络信息解析、信号名表、时长解析、十六进制转换），遂启动 libutils 共享代码重构。

**重构成果**：

| 阶段 | 新增模块 | 消除的重复 | 影响工具 |
|------|---------|-----------|----------|
| netinfo | `libutils/netinfo.c` | ip/ifconfig/route/netstat 的 /proc/net/dev 解析 + ioctl + MAC/IP 格式化 | 4 个 |
| utils_init | `libutils/version.c` | 35+ 工具的手写 version/program_name/--version/--help 样板 | 35+ 个 |
| duration | `libutils/duration.c` | sleep/timeout 的本地 parse_duration | 2 个 |
| signame | `libutils/signame.c` | kill/timeout 的本地信号名表（20→31 信号） | 2 个 |
| hex | `libutils/hex.c` | md5sum/sha256sum 的本地 hex 转换函数 | 2 个 |

**修复的预存 bug**：

1. **MD5 输出字节序交错** — `md5_final()` 的输出循环使用 `out[i*4]` 将 a/b/c/d 的字节交错排列（a 的 4 字节散布在 0/4/8/12 位置），导致哈希值完全错误。修正为顺序排列 `out[i]`/`out[4+i]`/`out[8+i]`/`out[12+i]`。
2. **MD5 + SHA-256 的 bits padding 污染** — `final()` 调用 `update()` 添加 padding 时也更新了 `c->bits`，导致追加的长度值包含 padding 字节数而非原始数据长度。修复：在 padding 前保存 `saved_bits`。
3. **check 模式 sscanf 解析** — `%*2s` 会跳过空格后吃掉文件名前两个字符（如 `/tmp/test.txt` → `mp/test.txt`）。修正为 `%32s %255s`，并添加二进制模式 `*` 前缀处理。

**验证**：md5sum/sha256sum 输出与 GNU 工具完全一致（空文件、大文件、stdin 均验证），check 模式可与 GNU 输出交叉验证。
