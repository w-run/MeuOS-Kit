# worktree-shell-utils — Agent 入口

> 其他 Agent 加入此 worktree 时，先读这个文件了解上下文。
>
> **最后更新**: 2026-08-03（libutils 重构 + 哈希 bug 修复）

## 这是什么

`worktree-shell-utils` 是 MeuOS Kit 的 **Phase 7（用户空间）启动 worktree**。

**目标**：从零实现 `projects/meuos-shell/`（msh）和 `projects/meuos-utils/`（核心工具集），与 Phase 0-5 已完成的 mcc/libc/meow/toolchain/sysroot 衔接，闭环自举链。

**不在本 worktree 范围**：`mcc`、`meuos-libc`、`meow`、`meuos-toolchain`、`meuos-sysroot`、`meuos-buildtools`（各自有独立 worktree）。

---

## 交接摘要（2026-08-03）

### 当前状态：P0-P8 全部完成，P10 libutils 重构完成，P9 规划中

上一会话完成了以下工作：

1. **ip/nslookup/telnet 网络工具** — ip addr/link/route/neigh + DNS 多类型查询 + Telnet IAC 协议
2. **netinfo 共享模块** — 提取 ip/ifconfig/route/netstat 4 工具的公共网络信息逻辑到 `libutils/netinfo.c`
3. **P0 utils_init 一站式初始化** — 消除 35+ 工具的手写 version/program_name 样板。`utils_init(argc, argv)` 自动处理 --version/--help + 设置 program_name + 返回 argi
4. **P1 parse_duration 时长解析** — 提取到 `libutils/duration.c`，增强复合时长（1h30m）和冒号格式（1:30:00）。重构 sleep/timeout
5. **P2 signame 信号名表** — 提取到 `libutils/signame.c`，信号表 20→31 信号 + `sig_from_name`/`sig_to_name`/`sig_list_all`。重构 kill/timeout
6. **P3 hex 十六进制转换** — 提取 `bytes_to_hex`/`hex_to_bytes` 到 `libutils/hex.c`。重构 md5sum/sha256sum
7. **md5sum/sha256sum 哈希算法 bug 修复** — 修复 3 个预存 bug：
   - MD5 输出字节序交错（`out[i*4]` → `out[i]`/`out[4+i]`/`out[8+i]`/`out[12+i]`）
   - MD5+SHA-256 的 `c->bits` 被 padding 污染（保存 `saved_bits`）
   - check 模式 sscanf `%*2s` 吃掉文件名前两个字符（改为 `%32s %255s` + `*` 前缀处理）

### 提交历史

```
98003d2 P3 hex 提取 + md5sum/sha256sum 3 bug 修复
bfa80fb P2 signame 提取
2339bbb P1 parse_duration 提取
3272c4f P0 utils_init 统一初始化
90abdf7 netinfo 共享模块
6e09a43 ip/nslookup/telnet 网络工具
```

### 测试状态

| 组件 | 命令 | 结果 |
|------|------|------|
| utils | `make -C projects/meuos-utils check` | ✅ 全部通过 |
| md5sum | 与 GNU md5sum 交叉验证 | ✅ 完全一致（空/大/stdin/check） |
| sha256sum | 与 GNU sha256sum 交叉验证 | ✅ 完全一致（空/大/stdin/check） |

---

## 关键技术踩坑（下一 Agent 必读）

### 1. libmz 返回值约定

`mz_compress()` 和 `mz_decompress()` 的返回值：

| 返回值 | 含义 |
|--------|------|
| **正数** | 成功，值为压缩/解压后的大小 |
| **0 或负数** | 失败（`MZ_OK`=0, `MZ_ERR_*`=-1~-6） |

**错误用法**：`if (rc != MZ_OK)` ← 压缩成功时 rc 是正数，会误判为失败

**正确用法**：`if (rc <= 0)` ← 仅负数或 0 为失败

### 2. MZ_CODEC_AUTO 不嗅探 MEUOS combo 格式

`mz_decompress(data, len, &out, &outlen, MZ_CODEC_AUTO)` 直接走 raw LZ77 路径，
**不会**自动检测 MEUOS combo 格式（首字节是 entropy_type）。

**正确做法**：先试 `MZ_CODEC_MEUOS`，失败再回退 `MZ_CODEC_LZ77`：
```c
int rc = mz_decompress(in, inlen, &out, &outlen, MZ_CODEC_MEUOS);
if (rc <= 0) {
    free(out); out = NULL; outlen = 0;
    rc = mz_decompress(in, inlen, &out, &outlen, MZ_CODEC_LZ77);
}
```

### 3. mxa_close() 仅用于读上下文

`mxa_close()` 内部将 ctx 强转为 `struct mxa_read_ctx *`。
对**写上下文**（`mxa_create()` 返回的 ctx）调用 `mxa_close()` 会导致段错误。

`mxa_finish()` 会分配新缓冲区给 `*result`，写上下文的内部缓冲仍在 ctx 中。
写上下文目前**没有公共的释放函数**——进程退出时自动回收即可（CLI 短命进程）。

如果需要长期运行的场景，需要给 libmz 添加 `mxa_write_close()` 函数。

### 4. Shell 编译零警告

修复方法参考：
- `snprintf` 截断警告 → 检查返回值 `(size_t)n >= sizeof(buf)` 后提前 return
- `strncpy` 截断警告 → 改用 `snprintf` + 手动 null 终止
- `int` vs `sizeof` 符号比较 → `int` 改 `size_t`

### 5. MD5/SHA-256 哈希算法的 padding 陷阱（已修复）

`final()` 函数调用 `update()` 添加 padding 时，会更新 `ctx->bits`（消息总位数）。
如果直接用 `ctx->bits` 生成追加的长度字段，长度值会包含 padding 字节数，导致哈希错误。

**正确做法**：在 padding 前保存原始 bit 数：
```c
uint64_t saved_bits = ctx->bits;
// ... 添加 padding ...
for (int i = 0; i < 8; i++) lenbuf[i] = saved_bits >> ...;
```

### 6. MD5 输出字节序（已修复）

MD5 的状态是 4 个 uint32_t（a/b/c/d），输出时需要按小端序写入 16 字节。
错误的写法 `out[i*4]` 会把 4 个字交错排列（a 的字节散布在 0/4/8/12），
必须用顺序排列 `out[i]`/`out[4+i]`/`out[8+i]`/`out[12+i]`。

---

## P9 — 压缩统一架构（下一阶段工作）

### 架构决策

> 将所有压缩/解压算法收归 `meuos-compress`（libmz）库，
> `gzip`/`unzip`/`tar` 等工具变为薄壳调用 libmz。

### 实施路径（按顺序）

| 序号 | 任务 ID | 范围 | 依赖 |
|------|---------|------|------|
| 1 | `mz-deflate-codec` | 在 `meuos-compress/src/codec/` 新增 `mz_deflate.c`，实现 RFC 1951 DEFLATE 压缩/解压。在 `mz.h` 新增 `MZ_CODEC_DEFLATE`。`mz_compress()`/`mz_decompress()` 增加 switch 分支。 | 无 |
| 2 | `mz-gzip-format` | 在 `meuos-compress/src/container/` 新增 `mz_gzip.c`，封装 gzip 格式（RFC 1952：header + DEFLATE data + CRC32 + size）。提供 `mz_gzip_compress()` / `mz_gzip_decompress()` API。 | 1 |
| 3 | `gzip-thin-shell` | 重构 `meuos-utils/src/utils/gzip.c`：删除内联 DEFLATE 实现（~250行），改为调用 libmz 的 `mz_gzip_compress()` / `mz_gzip_decompress()`。保留 gzip CLI 接口不变。 | 2 |
| 4 | `mz-pkzip-container` | 在 `meuos-compress/src/container/` 新增 `mz_zip.c`，封装 PKZIP 格式（中央目录解析 + local header + CRC32）。提供类似 mxa 的 `mz_zip_open()` / `mz_zip_list()` / `mz_zip_read_file()` API。 | 1 |
| 5 | `unzip-thin-shell` | 重构 `meuos-utils/src/utils/unzip.c`：删除内联 DEFLATE + PKZIP 解析（~650行），改为调用 libmz。保留 unzip CLI 接口不变。 | 4 |
| 6 | `tar-mz-support` | 在 `meuos-utils/src/utils/tar.c` 增加 `-Z` 选项：两步模式（tar 打包 → mz 压缩为 .tar.mz）。解包时自动检测 .tar.mz 后缀。 | 无（可独立做） |

### 预期收益

| 指标 | 当前 | 重构后 |
|------|------|--------|
| gzip.c 行数 | ~400（含 DEFLATE） | ~80（薄壳） |
| unzip.c 行数 | ~650（含 DEFLATE + PKZIP） | ~150（薄壳） |
| DEFLATE 实现份数 | 2（gzip + unzip 各一份） | 1（libmz 统一） |
| 压缩算法升级影响面 | 3 个文件 | 1 个文件（libmz） |

### 注意事项

- DEFLATE codec 实现可参考 gzip.c 中已有的 inflate 代码（已验证正确），提取到 libmz 中
- CRC32 在 gzip.c 和 unzip.c 中各有一份，应统一到 libmz
- PKZIP 容器格式可参考 mxa 的实现模式（create/open/list/read/close API 风格）
- 重构时保持 CLI 行为完全不变（`--help`/`--version`/选项语义不变）

---

## 任务队列

所有任务在 `.issues/INDEX.md` 中按优先级分组：

| 优先级 | 内容 | 状态 |
|--------|------|------|
| **P0-skeleton** | 两组件骨架 | ✅ 完成 |
| **P1-coreutils** | cat/echo/true/false/yes/test | ✅ 完成 |
| **P2-fileutils** | cp/mv/rm/ls/mkdir/rmdir/ln/touch/chmod/chown | ✅ 完成 |
| **P3-textutils** | head/tail/wc/sort/uniq/cut/tr/tee/dd | ✅ 完成 |
| **P4-diff/find** | diff/cmp/patch/find/xargs/grep/locate | ✅ 完成 |
| **P5-archive** | tar + gzip + mz + unzip | ✅ 完成 |
| **P6-shell-core** | msh POSIX sh 子集 | ✅ 完成 |
| **P7-shell-interactive** | msh 行编辑/历史/Tab/作业控制 | ✅ 完成 |
| **P8-shell-bash** | msh 可选 bash 兼容 + zsh 插件 | ✅ 完成 |
| **P9-compress-unify** | 压缩算法统一收归 libmz | ⏳ 规划中 |
| **P10-libutils-refactor** | libutils 共享代码重构 + 哈希 bug 修复 | ✅ 完成 |

---

## 设计原则（快速参考）

1. **§4 禁止事项强约束**：零 glibc 专有符号 / 零 GNU 代码 / 零 autotools / 系统调用直走 `syscall()` 或内联汇编 / 构建可重现
2. **§6.1 三阶段路径**：标准化可用 → 有利特性 → 性能优化
3. **§6.2 参考资源（节省算力）**：Shell → dash/serenityOS Shell；Utils → uutils/BusyBox/serenityOS Utilities。不复制源码，用自己的代码重新实现
4. **§4 简单 Makefile**：每个组件用简单 Makefile 构建，**禁止 autotools/cmake/meson**
5. **§5.2 sysroot 约定**：`MEUOS_SYSROOT` 须指向架构子目录 `sysroot/<arch>`（非顶层）
6. **多调用二进制（BusyBox 风格）是可选**：首期按每工具独立二进制实现，未来可补多调用支持

## 关键文件索引

| 路径 | 内容 |
|------|------|
| `AGENTS.md` | 项目全局规约（会话恢复流程优先度最高） |
| `.issues/AGENT.md` | 本文件，worktree 入口 |
| `.issues/INDEX.md` | 任务队列 + 设计文档 |
| `projects/meuos-utils/ARCHITECTURE.md` | utils 项目结构 + 路线图 + §12.3 压缩外包决策 |
| `projects/meuos-shell/ARCHITECTURE.md` | msh 项目结构 + 路线图 |
| `projects/meuos-utils/src/utils/mz.c` | mz 工具源码（libmz 封装） |
| `projects/meuos-utils/Makefile` | utils Makefile（含 libmz 链接规则） |
| `projects/meuos-compress/` | libmz 库（codec + container + crypt） |
| `projects/meuos-utils/.todo/` | utils 子任务详细设计 |
| `projects/meuos-shell/.todo/` | msh 子任务详细设计 |
| `pkgs/dash/` | dash 0.5.12 构建配方（POSIX sh 参考实现） |
| `reference/` | cproc/QBE/musl 参考（gitignored） |
| `env/` | QEMU 测试环境（已链接自 main） |

## 工作树特殊状态

- 从 `main` 拉出（不含 worktree-stable-enhance 的 .issues/INDEX.md/AGENT.md）
- 拥有全新的 `.issues/AGENT.md` + `.issues/INDEX.md`（本 worktree 专属）
- 不跟踪其他 worktree 的工作，所有变更只 push 到 origin/worktree-shell-utils

## 典型 Agent 启动流程

```sh
# 1. 读 AGENTS.md（项目全局规约）
# 2. 读 .issues/AGENT.md（本入口文件）← 你正在读
# 3. 读 .issues/INDEX.md（任务队列）
# 4. 确认 MEUOS_SYSROOT 已设置（须指向 sysroot/<arch>）
# 5. 选定任务（建议从 P9 开始），开始实施
```

## 工作纪律

每次任务执行结束后**必须执行**以下步骤：

### 1. 验证
```sh
make -C projects/meuos-utils check        # 或 projects/meuos-shell/check
make -C projects/meuos-utils check-sysroot-static   # 可选，验证 mcc 路径
```

### 2. 提交变更
```sh
git add -A
git commit -m "<component>: <task-id> <描述>"
# 提交到 worktree-shell-utils，不合并到 main
git push origin worktree-shell-utils
```

### 3. 更新文档
- 完成任务：在 `.issues/INDEX.md` 把状态从 ⏳/🔴 改成 🟢/✅
- 新增任务：追加到 `.issues/INDEX.md` 对应优先级
- 设计决策/踩坑：追加到 `projects/<组件>/ARCHITECTURE.md` 的「实施笔记」段或对应 `.todo/` 文件

### 4. 重读约束

回到本文件 + `.issues/INDEX.md` 设计原则段，防止上下文过长导致对以下约束的记忆衰减：
- §4 禁止事项（glibc/GNU/autotools）
- §6.1 三阶段路径（先标准化、再特性、再优化）
- §6.2 不复制源码，用自己的手重写
- 多调用二进制为可选（每工具独立二进制首期）

## 执行策略

| 任务类型 | 策略 |
|---------|------|
| **工具实现（cat/echo 等独立工具）** | 单文件单 PR，参考 busybox/uutils 对应源码但重写，提交粒度 = 1 个工具 |
| **msh 模块实现（lex/parse/exec/builtin）** | 按依赖 DAG 串行 + 阶段内并行 hy3 无头 agent |
| **跨组件验证** | mcc + sysroot 路径通过 `make -C projects/<comp> check-sysroot-static` 验证 |
| **Makefile 调整** | 保持简单 Makefile（§4），不引入 autotools/cmake |
| **P9 压缩统一** | 按 6 步顺序实施，每步独立提交，每步 `make check` 验证 |

## 任务 ID 命名规则

格式：`<组件>-<工具/模块>-<修饰>`

- `utils-cat`, `utils-cp`, `utils-grep`, `utils-tar`, `utils-mz`
- `msh-lex`, `msh-parse`, `msh-exec`, `msh-var`, `msh-builtin`, `msh-history`, `msh-lineedit`, `msh-job`
- `mz-deflate-codec`, `mz-gzip-format`, `gzip-thin-shell`, `mz-pkzip-container`, `unzip-thin-shell`, `tar-mz-support`

全局唯一，可直接用作 git 提交引用。

## 工作量估算系数

| 任务类型 | 单任务估算 |
|---------|-----------|
| 简单工具（cat/echo/true） | 1-4 小时 |
| 中等工具（cp/mv/ls/find） | 4-16 小时 |
| 复杂工具（grep/sed/tar/diff） | 1-3 天 |
| msh 词法/语法/执行 各阶段 | 1-2 周/阶段 |
| msh 交互层（readline 替代） | 1-2 周 |
| msh bash 兼容层 | 持续投入，不设截止 |
| **P9: DEFLATE codec** | 1-2 天（参考 gzip.c 已有实现） |
| **P9: PKZIP 容器** | 2-3 天（参考 mxa 已有实现） |
| **P9: gzip/unzip 薄壳重构** | 各 0.5 天 |
