# .msys 单文件 sysroot — 实现任务

> 归宿：`projects/meuos-sysroot/` — 独立的单文件 sysroot 组件
> 消费方：mcc（头文件）、mt/ld（库/对象）、meow（打包）

## 格式定义

```
Header (32 bytes):
  [0..7]  Magic: "Msys1\0\0\0"
  [8..15] Index offset (uint64 LE) — 从文件头到索引块的偏移
  [16..19] Index count (uint32 LE)
  [20..23] Flags (uint32 LE)
  [24..31] Reserved (zero)

Index entry (16 + name_len bytes, sorted by name_hash):
  [0..3]  name_hash (uint32 LE, FNV-1a 32-bit)
  [4..9]  data_offset (uint48 LE)
  [10..13] data_size (uint32 LE)
  [14..15] name_len (uint16 LE)
  [...]   name (name_len bytes, no NUL terminator)

Data blocks: 按 data_offset 排列，4 字节对齐

元数据条目: 以 '@' 前缀命名的条目
  @meuos_arch      目标架构名称（如 aarch64, x86_64）
```

## 任务清单

### Phase 1A — libmsys 核心库 ✅

- [x] `include/mt/msys.h`：声明 `msys_open()`, `msys_close()`, `msys_read()`, `msys_search()`
- [x] `src/libmsys/msys.c`：mmap → 解析 header → 索引二分查找 → 返回数据指针
- [x] 集成到 Makefile：产出 `build/libmsys.a`

### Phase 1B — mkmsys 打包工具 ✅

- [x] `src/mkmsys/main.c`：遍历目录 → FNV-1a 哈希 → 排序 → 写入 .msys
- [x] 支持 `-o <output>`、`--list` 列出内容
- [x] 支持 `--arch <name>` 写入元数据键 `@meuos_arch`

### Phase 2 — mcc 集成 ✅

- [x] `mcc/src/driver/msys.c`：sysroot 抽象层，检测 `.msys` 后缀走 libmsys 读取
- [x] preprocessor include 搜索支持 .msys
- [x] 未显式传 `--target` 时，从 .msys 提取 `@meuos_arch`

### Phase 3 — mt/ld 集成 ✅ 已完成

- [x] mt/ld 的 `--sysroot=<path>.msys` 自动解包到 temp 目录
- [x] mt/ld 链接 libmsys.a
- [x] 端到端验证通过（`make check` + `check-i386-e2e`）

### Phase 4 — 压缩 + VFS 改进 ✅

- [x] `msys.h`：添加 `MSYS_F_ZLIB`、`MSYS_F_ZSTD`、`MSYS_F_INCREMENTAL` 压缩标志
- [x] `mkmsys`：添加 `--compress=zlib` 实际压缩（dlopen 动态加载 libz，逐块 deflate）
- [x] `mkmsys`：压缩不会膨胀——压缩后比原件大则存原件
- [x] `msys_fopen` 支持压缩：解压后注册到 chunks 链表，fclose 时释放
- [x] `msys_load` 支持压缩：解压后直接返回分配内存
- [x] `msys_close`：遍历 chunks 链表释放所有解压缓冲
- [x] `msys.h`：`struct msys` 添加 `struct msys_chunk *chunks` 链表
- [x] `make msys`：自动从 sysroot 目录生成 .msys
- [x] `make check-msys`：验证生成的 .msys 可被正确读取
- [x] `mkmsys --incremental` 实际增量逻辑（对比 @mt mtime，未变文件从旧存档读取）
- [x] `mkmsys --compress=zstd` 实际压缩逻辑（dlopen libzstd.so, ZSTD_compress）
- [x] `bootstrap.sh` 阶段产出 .msys（Phase 4 验证通过后自动生成）

### Phase 5 — v2 格式设计（实现中）

> v2 格式已在 Phase 2A-2C 中实现：向后兼容阅读器、v2 写入器、目录块。
> 以下为子阶段进展。

#### 5.1 目录层次结构 ✅

- [x] `msys_readdir(m, "/usr/lib")` 返回目录下文件和子目录列表
- [x] 实现方式：v2=目录块 O(1) 哈希查找 / v1=前缀扫描 O(N)
- [x] 消费方：msysctl 统一 CLI（cat/ls/find/tree/extract/verify/stat）
- [x] libmsys 直接读取，无需解压到临时目录

#### 5.2 完整文件元数据 ✅

- [x] 扩展索引条目（v2: 32B/entry 含 file_type/mode/uid/gid）
- [x] 文件类型：普通文件/目录/符号链接/设备节点/管道
- [x] 权限位（`0775` 等 Unix 权限）
- [x] uid/gid（数字）
- [x] mtime（通过 @mt/ 元数据存储）
- [x] 可选 xattr（@xattr/<name> 条目，`msys_getxattr()` API）
- [x] 打包时自动收集 xattr（sys/xattr.h）

#### 5.3 符号链接 ✅

- [x] file_type=SYMLINK，数据块存储目标路径
- [x] `msys_readlink()` API 返回链接目标
- [x] `msys_load()` 自动解析符号链接（递归跟随，8 层深度限制）

#### 5.4 SHA-256 + 去重 ✅

- [x] 内容哈希（SHA-256）索引，相同内容只存一份（sha256.c 独立实现）
- [x] 打包时去重检查，相同文件在不同路径下共用数据（`--dedup` 标志）
- [x] `msys_verify()` / `msys_verify_all()` API（libmsys 端已实现）

#### 5.5 文件级校验（与 5.4 合并）

#### 5.6 分层 / Overlay ✅

- [x] 支持堆叠多个 .msys：基础层 + 用户层（`msys_overlay_open/add`）
- [x] 同名文件覆盖语义（搜索从高层到底层，首次匹配返回）
- [x] `msys_overlay_search/read/stat/readlink/load/fopen/readdir/verify/close` API

#### 5.7 签名 / 认证 ✅

- [x] 扩展块 ed25519 签名（libsodium dlopen 实现）
- [x] `msys_verify_signature(pk)` API（验证索引块 SHA-256 + ed25519 签名）
- [x] `mkmsys --sign=<keyfile>`（32B seed 或 64B 完整密钥）

#### 5.8 流式消费 ✅

- [x] 支持按顺序流式读取出所有文件的（name, data）对（`msys_stream_*` API）
- [x] `MSYS_F_STREAMING` 顺序布局（`--streaming` 标志）

#### 5.9 扩展块机制 ✅

- [x] 索引块后跟 optional extension blocks（`type(4)+length(4)+data(length)`）
- [x] `msys_get_extension()` API（线性扫描，按 type 查找）
- [x] mkmsys 写入端支持扩展块（`ext_data`/`ext_len` 参数）

## 验收

```sh
# 打包（未压缩）
mkmsys -o /tmp/aarch64.msys /path/to/sysroot-aarch64

# 打包（压缩）
mkmsys --compress=zlib -o /tmp/aarch64-compressed.msys /path/to/sysroot-aarch64

# 查看内容（压缩/未压缩）
mkmsys --list /tmp/aarch64.msys
mkmsys --list /tmp/aarch64-compressed.msys

# mcc 读头文件
mcc --target=aarch64 --sysroot=/tmp/aarch64.msys -c -o test.o test.c
mcc --target=aarch64 --sysroot=/tmp/aarch64-compressed.msys -c -o test.o test.c

# 全链路
mcc --sysroot=/tmp/aarch64.msys -o test test.c

# 压缩比验证
ls -lh /tmp/aarch64.msys /tmp/aarch64-compressed.msys
```

## 参考

- FNV-1a 哈希算法：https://en.wikipedia.org/wiki/Fowler–Noll–Vo_hash_function
- 现有工具链中的 libelf 共享库模式：`meuos-toolchain/src/libelf/`
- SquashFS 格式设计（只读文件系统参考）
- OCI 镜像分层规范（分层/overlay 参考）
