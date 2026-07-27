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
| `--compress=zstd` | 🟡 桩 | flags 已定义，代码待实现 |
| `--incremental` 增量打包 | 🟡 桩 | CLI 已定义，逻辑待实现 |

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

### 文件布局

```
[Header (32 bytes)]
[Data block 1]       ← 4 字节对齐
[Data block 2]
...
[Data block N]
[Padding]            ← 对齐到 4 字节边界
[Index block]        ← 连续存放的变长索引条目
```

### Header (32 bytes)

| 偏移 | 大小 | 字段 | 说明 |
|------|------|------|------|
| 0 | 8 | magic | `"Msys1\0\0\0"` |
| 8 | 8 | index_offset | 从文件头到索引块的偏移 (uint64 LE) |
| 16 | 4 | index_count | 索引条目数 (uint32 LE) |
| 20 | 4 | flags | 标志位（见下方） |
| 24 | 8 | reserved | 保留，必须为 0 |

### Flags

| 标志 | 值 | 说明 |
|------|-----|------|
| `MSYS_F_NONE` | 0x00 | 无压缩 |
| `MSYS_F_ZLIB` | 0x01 | zlib deflate 压缩（单文件级别，非整体） |
| `MSYS_F_ZSTD` | 0x02 | zstd 压缩（预留，未实现） |
| `MSYS_F_INCREMENTAL` | 0x04 | 增量模式（预留，未实现） |

### Index entry (16 + name_len bytes)

| 偏移 | 大小 | 字段 | 说明 |
|------|------|------|------|
| 0 | 4 | name_hash | FNV-1a 32-bit, LE |
| 4 | 6 | data_offset | 数据块在文件中的偏移 (uint48 LE) |
| 10 | 4 | data_size | 数据块大小 (uint32 LE) |
| 14 | 2 | name_len | 文件名长度 (uint16 LE) |
| 16+ | name_len | name | 文件名（无 NUL 终止符） |

索引按 `name_hash` 升序排列，哈希冲突时按 `name` 字典序排列。

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
