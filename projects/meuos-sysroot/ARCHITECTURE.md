# meuos-sysroot — .msys 单文件 sysroot 系统

> 本组件定义和维护 `.msys` 格式：一种自定义单文件 sysroot，可直接被 mcc（头文件搜索）和
> mt/as、mt/ld（库/对象读取）原生读取，无需解压到文件系统。

## 当前能力

| 能力 | 状态 | 说明 |
|------|------|------|
| mmap 零拷贝读取 | ✅ | 打开即 mmap，提供数据指针 |
| FNV-1a 哈希索引 + 二分查找 | ✅ | O(log N) 查找 |
| 哈希冲突处理（碰撞线性扫描） | ✅ | 按 hash 分组后回退字符串比较 |
| 可变长度文件名（无 NUL 浪费） | ✅ | 16 字节固定头 + name_len 字节 |
| `msys_search()` 精确按名查找 | ✅ | 核心查找 API |
| `msys_read()` 读取到用户缓冲 | ✅ | msys_search + memcpy |
| `msys_fopen()` FILE* 接口 | ✅ | 零拷贝 fmemopen（未压缩时） |
| `msys_load()` 分配内存的加载 | ✅ | malloc + memcpy 或解压后直接返回 |
| zlib 整体压缩/解压 | ✅ | dlopen 动态加载 libz，~5:1 压缩比 |
| 压缩时 msys_fopen 支持 | ✅ | 解压到 chunks 链表，msys_close 时释放 |
| mkmsys 打包工具 | ✅ | 目录遍历 → 排序 → 写入 |
| `--arch` 元数据标记 | ✅ | 写入 `@meuos_arch` 键 |
| `--list` 索引查看 | ✅ | 列出所有条目的 hash/offset/size/name |
| `make msys` 自动化打包 | ✅ | 从 `$MEUOS_SYSROOT` 生成 .msys |
| `check-msys` 验证 | ✅ | 读取验证通过 |
| mcc 驱动集成 | ✅ | `msys_fopen` 回退搜索头文件 |
| mt/ld 集成 | ✅ | `msys_vfs_load` 回退加载库 |
| `-L .msys` 搜索 | ✅ | ld 自动识别 .msys 扩展名 |
| 内存版 archive 遍历 | ✅ | `mt_ar_foreach_mem()` |
| `--compress=zstd` | ✅ | dlopen libzstd，~4:1 压缩比 |
| `--incremental` 增量打包 | ✅ | @mt mtime 对比，不变文件复用旧数据 |
| v2 格式 (Msys2) | ✅ | 64B header, 32B index entry, dir block |
| v2 目录块 O(1) readdir | ✅ | 哈希截断 → 线性扫描 → O(dir_count) |
| v2 完整元数据 (mode/uid/gid) | ✅ | file_type/mode/uid/gid in v2 index |
| 符号链接 (SYMLINK) | ✅ | msys_readlink + msys_load 自动解析 |
| SHA-256 内容校验 | ✅ | msys_verify / msys_verify_all |
| 内容去重 (--dedup) | ✅ | SHA-256 → 相同数据共享 data_offset |
| 分层/Overlay | ✅ | msys_overlay_open/add/search/readdir |
| ed25519 签名 (--sign) | ✅ | 扩展块存储 + msys_verify_signature |
| 流式消费 (--streaming) | ✅ | msys_stream_open/next/close |
| 扩展块机制 | ✅ | type(4)+length(4)+data() after index |
| xattr 扩展属性 | ✅ | @xattr/<name> 条目 + msys_getxattr |
| msysctl 统一 CLI | ✅ | cat/ls/find/tree/extract/verify/stat |

## 设计原则

- **独立于任何组件**：meuos-sysroot 不依赖 mcc、mt/as、mt/ld，它是被它们消费的
- **格式自描述**：Magic + 排序索引，任何工具都可直接解析
- **目录兼容**：.msys 是补充而非替代，`--sysroot` 同时接受目录和 .msys
- **压缩可选**：通过 dlopen 动态加载压缩库，运行时决定是否压缩
- **内存自主管理**：解压缓冲通过 `struct msys_chunk` 链表跟踪，close 时统一释放

## 目录结构

```
meuos-sysroot/
├── Makefile               # 构建 libmsys.a + mkmsys
├── ARCHITECTURE.md        # 本文件
├── .todo/
│   └── msys.md            # 实现任务清单
├── include/mt/
│   └── msys.h             # libmsys 公共接口（mcc、mt 共享）
├── src/
│   ├── libmsys/
│   │   └── msys.c         # .msys 读取核心（mmap + 索引二分查找）
│   └── mkmsys/
│       └── main.c         # 从 sysroot 目录生成 .msys 的打包工具
└── test/
    └── msys_test.c        # 读写验收测试
```

## 格式规范

### v1 格式（32 字节 header，16+name_len 索引条目）

```
[Header (32 bytes)]
[Data block 1]       ← 4 字节对齐
[Data block 2]
...
[Data block N]
[Padding]            ← 对齐到 4 字节边界
[Index block]        ← 连续存放的变长索引条目
```

### v2 格式（72 字节 header，32+name_len+opt 索引条目）

```
[Header (72 bytes)]
[Data block 1]       ← 4 字节对齐
[Data block 2]
...
[Data block N]
[Directory block]    ← NEW: O(1) 目录列表
[Index block]        ← 32B/entry + name + 可选 SHA-256
[Extension blocks]   ← NEW: type(4)+length(4)+data() 链
```

### v2 Header (72 bytes)

| 偏移 | 大小 | 字段 | 说明 |
|------|------|------|------|
| 0 | 8 | magic | `"Msys2\0\0\0"` |
| 8 | 8 | index_offset | 索引块偏移 (uint64 LE) |
| 16 | 4 | index_count | 条目数 (uint32 LE) |
| 20 | 4 | flags | 标志位 |
| 24 | 8 | dir_offset | 目录块偏移，0=无 (uint64 LE) |
| 32 | 4 | dir_count | 目录条目数，0=无 (uint32 LE) |
| 36 | 4 | extension_offset | 扩展块偏移，0=无 (uint32 LE) |
| 40 | 8 | data_size_total | 所有未压缩数据总和 (uint64 LE) |
| 48 | 8 | content_hash | 索引块 SHA-256 前 8 字节 |
| 56 | 16 | reserved | 保留，必须为 0 |

### v2 Index Entry (32 + name_len + 可选 32 字节)

| 偏移 | 大小 | 字段 | 说明 |
|------|------|------|------|
| 0 | 4 | name_hash | FNV-1a 32-bit, LE |
| 4 | 6 | data_offset | 数据块偏移 (uint48 LE) |
| 10 | 4 | data_size | 存储大小 (uint32 LE, 压缩时=压缩后大小) |
| 14 | 4 | uncompressed_size | 原始大小 (uint32 LE, 0=未压缩) |
| 18 | 2 | file_type | 文件类型 (uint16 LE: MSYS_FILE_*) |
| 20 | 2 | mode | 权限 (uint16 LE) |
| 22 | 4 | uid | 属主 (uint32 LE) |
| 26 | 4 | gid | 属组 (uint32 LE) |
| 30 | 1 | name_len | 名称长度 (uint8, 最大 255) |
| 31 | 1 | content_hash_present | 0=无, 1=末尾追加 32 字节 SHA-256 |
| 32+ | name_len | name | 文件名（无 NUL 终止符） |

### 目录块条目 (4 + name_len 字节)

| 偏移 | 大小 | 字段 | 说明 |
|------|------|------|------|
| 0 | 2 | parent_hash_trunc | 父路径 FNV-1a 哈希的高 16 位 |
| 2 | 1 | name_len | 名称长度 (uint8) |
| 3 | 1 | entry_type | 类型 (0=文件, 1=目录, 2=符号链接) |
| 4+ | name_len | name | 条目名称 |

### 元数据条目

以 `@` 前缀命名的条目是元数据键值对。当前支持的元数据：
- `@meuos_arch`：目标架构名称（如 `aarch64`、`x86_64`）

## 压缩方案

- **写路径**（mkmsys `--compress=zlib`）：dlopen `libz.so.1`，逐数据块 deflate。如果压缩后比原始还大，则存储原始数据。
- **读路径**（msys_fopen/msys_load）：检测 `flags & MSYS_F_ZLIB`，调用 dlopen 加载的 `inflate` 解压。解压后缓冲通过 `struct msys_chunk` 链表注册到 `struct msys`，`msys_close` 时统一释放。
- **好处**：无需编译时链接 libz，系统有 libz 则用，没有则回退到未压缩。

## VFS 抽象层

libmsys 提供三种消费接口，按调用方需求选择：

```
消费方接口        | 未压缩（零拷贝） | 压缩（需解压）
msys_search()     | 返回 mmap 指针   | ❌ 不适用
msys_fopen()      | fmemopen 零拷贝  | 解压 → chunks 注册 → fmemopen
msys_load()       | malloc + memcpy  | 解压 → 直接返回分配的内存
```

## 依赖关系

```
meuos-sysroot（本组件）
    ↑ 链接 libmsys.a
    ├── mcc           — preprocessor include 搜索（msys_fopen）
    ├── mt/ld         — -L 参数识别 .msys，读取 .a/.o（msys_load）
    └── meow          — make msys 调用 mkmsys 打包
```

## 使用方式

```sh
# 打包（未压缩）
mkmsys -o meuos-aarch64.msys sysroot-aarch64

# 打包（压缩）
mkmsys --compress=zlib -o meuos-aarch64-compressed.msys sysroot-aarch64

# 查看内容
mkmsys --list meuos-aarch64.msys         # 列出所有路径
mkmsys --list meuos-aarch64-compressed.msys  # 压缩包同样可查看

# mcc 读头文件（未压缩/压缩均支持）
mcc --target=aarch64 --sysroot=meuos-aarch64.msys -c -o test.o test.c

# 全链路
mcc --target=aarch64 --sysroot=meuos-aarch64.msys -o test test.c
```

## 局限性（与未来方向对比）

当前 .msys 设计定位于**开发工具链 sysroot**，不是通用系统镜像格式。主要局限：

| 缺失特性 | 障碍 | 未来方向 |
|----------|------|---------|
| 目录层次结构 | 只能精确按名查找 | 添加 `msys_readdir()` |
| 文件元数据（类型/权限/uid/mtime） | 只存了文件名和数据 | 扩展索引条目 |
| 符号链接 | 没有 symlink 语义 | 添加文件类型字段 |
| 内容寻址/去重 | 相同内容重复存储 | 添加内容哈希索引 |
| 文件级校验 | 无完整性保护 | 索引中加 CRC32/SHA-256 |
| 分层/Overlay | 一次只能打开一个 | 支持多个 .msys 叠加 |
| 签名/认证 | 完全不验证发布者 | 尾部附加签名块 |
| 流式消费 | 必须 mmap 全文件 | 支持顺序流式读取 |

详见 `.todo/msys.md` Phase 5。
