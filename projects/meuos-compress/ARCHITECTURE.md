# mz — MeuOS Compress 架构文档

算法代号：**meuos-compress**，产物名：**mz**（libmz.a）。

## 目录

1. [设计哲学](#1-设计哲学)
2. [目录结构](#2-目录结构)
3. [容器格式规范](#3-容器格式规范)
4. [meuos-compress 统一算法](#4-meuos-compress-统一算法)
5. [模块职责](#5-模块职责)
6. [固实压缩（Solid）](#6-固实压缩solid)
7. [加密方案](#7-加密方案)
8. [msys 集成](#8-msys-集成)
9. [数据流](#9-数据流)

---

## 1. 设计哲学

**mz** 不是 "又一个压缩库"。它的定位是一个**融合社区算法精华、按 MeuOS 场景演进的自有算法引擎**。就像 mcc 融合 cproc 前端 + QBE 后端后演进出独有特性一样，meuos-compress 也走这条路：

- **Layer 1 — 取经**：精读社区最佳实现的精简核心，提取算法思想
- **Layer 2 — 融合**：将不同策略整合到一个引擎中，按 level / 数据特征自动切换
- **Layer 3 — 演进**：根据 MeuOS 实际场景（msys/kernel/initramfs/backup）持续添加自有特性

### 社区参考图

| 策略 | 参考来源 | 吸收什么 | MeuOS 独有 |
|------|---------|---------|-----------|
| 快速匹配 (level 1-3) | LZ4 的 byte-level hash + token 格式 | 极简匹配管线和格式设计 | + 内置 ChaCha20 |
| 深度匹配 (level 4-6) | zstd 的 hash chain + lazy matching | 三级 hash + 链深度渐进 | + 自有容器/文件表 |
| 极限压缩 (level 7-9) | zstd 的 optimal parse + tANS | 最优解析 + tANS 熵编码 | + Ed25519 签名 |
| 固实压缩 | zstd --train 字典思想 | 共享 history buffer | + 自有容器固实文件表 |

### 核心原则

- **一个算法代号**：对外统一叫 `meuos-compress`（`MZ_CODEC_MEUOS`），社区痕迹不暴露在 API 中
- **渐进式，非替换式**：level 1→9 不是换算法，而是**同一个引擎内部逐步叠加**更深的匹配、更强的熵编码、更优的解析
- **零外部依赖**：纯 C11，kernel 可直接编译使用
- **产物名 mz**：二进制/库名保持 `libmz.a`，兼容现有构建

### 长期演进路径

**兼容只是起点，衍生出我们自己的算法才是长期未来。**

mz 的路线和 mcc 编译器完全一致：

```
mcc 路径：  cproc + QBE 集成  →  融合成一个二进制  →  演进出自有特性
           ↓                    ↓                    ↓
mz 路径：  社区算法取经融合    →  统一 meuos-compress →  演进出自有算法
```

每一轮迭代，我们的算法离社区参考就越远、离 MeuOS 场景就越近：

| 阶段 | 社区依赖度 | MeuOS 独有度 | 说明 |
|------|:---------:|:------------:|------|
| Phase A | 高 | 低 | 取经阶段：LZ4/zstd 思想为主 |
| Phase B | 中 | 中 | 融合阶段：自有容器+加密+调度 |
| Phase C | 低 | 高 | 演进阶段：固实+字典+BCJ 自有实现 |
| Phase D+ | 极低 | 极高 | **衍生出自有算法**，社区只是历史参考 |

最终目标：`meuos-compress` 在压缩比/速度/场景适应性上形成自己的独特 trade-off，别人反过来参考我们的设计。

---

## 2. 目录结构

```
projects/meuos-compress/
├── ARCHITECTURE.md          # 本文档
├── Makefile                  # 构建文件
├── include/
│   └── mz.h                  # 公共 API 头文件
├── src/
│   ├── mz_core.c             # 容器格式、block 读写、文件表
│   ├── mz_lz77.c             # LZ77 匹配/编解码（改进版）
│   ├── mz_huf.c              # Huffman 编码/解码
│   ├── mz_ans.c              # tANS 编码/解码（level 7+）
│   ├── mz_crypt.c            # ChaCha20 + Ed25519 封装
│   ├── mz_solid.c            # 固实压缩上下文管理
│   └── mz_main.c             # mz_compress/mz_decompress 入口 + level 调度
└── test/
    └── test.c                # 测试
```

### 构建产物

- `build/libmz.a` — 静态库
- `build/test_mz` — 测试可执行

---

## 3. 容器格式规范

### 3.1 文件布局

```
┌─────────────────────────────────┐
│  Magic: "MZv2" (4 字节)        │
├─────────────────────────────────┤
│  Header (8 字节)               │
│  ├─ version (1 字节)           │
│  ├─ level   (1 字节)           │
│  └─ flags   (2 字节)           │
├─────────────────────────────────┤
│  Block Chain (变长)            │
│  ├─ Block 0                    │
│  ├─ Block 1                    │
│  ├─ ...                        │
│  └─ File Table Block (最后)    │
└─────────────────────────────────┘
```

### 3.2 Magic + Header

| 偏移 | 大小 | 字段 | 说明 |
|------|------|------|------|
| 0    | 4    | magic      | 固定值 `0x4D5A7632`（ASCII "MZv2"） |
| 4    | 1    | version    | 格式版本，当前 = 2 |
| 5    | 1    | level      | 压缩级别 1~9 |
| 6    | 2    | flags      | 位标志（见下） |

**flags 位定义**：

| 位 | 名称 | 说明 |
|----|------|------|
| 0  | MZ_FLAG_SOLID   | 固实压缩 |
| 1  | MZ_FLAG_ENCRYPT | 启用 ChaCha20 加密 |
| 2  | MZ_FLAG_SIGNED  | 包含 Ed25519 签名 |
| 3~15 | 保留 | 必须为 0 |

### 3.3 Block 链

每个 block 由 4 字节头部 + 变长数据组成：

```
┌─────────────────────────────┐
│  Block头部 (4 字节)         │
│  ├─ type (1 字节)           │
│  └─ size (3 字节，大端)     │
├─────────────────────────────┤
│  Block数据 (size 字节)      │
└─────────────────────────────┘
```

**Block type 枚举**：

| 值  | 名称          | 说明 |
|-----|---------------|------|
| 0   | RAW           | 原始未压缩数据 |
| 1   | LZ77_HUFF     | LZ77 + Huffman 编码数据 |
| 2   | SOLID_START   | 固实流的第一个文件（重置字典） |
| 3   | SOLID_NEXT    | 固实流的后续文件（共享字典） |
| 4   | ENCRYPTED     | ChaCha20 加密的 block |
| 5   | SIGNED        | Ed25519 签名 block |

Block 大小字段为 3 字节大端无符号整数，最大 16MB（0xFFFFFF）。

### 3.4 文件表 Block

文件表是特殊 block（type=0 RAW），其数据区为文件记录序列：

```
┌───────────────────────────────┐
│  条目计数 (4 字节 LE)         │
├───────────────────────────────┤
│  条目 0                       │
│  ├─ name_len (2 字节 LE)      │
│  ├─ name     (name_len 字节)  │
│  ├─ uid      (4 字节 LE)      │
│  ├─ gid      (4 字节 LE)      │
│  ├─ mode     (2 字节 LE)      │
│  ├─ size     (8 字节 LE)      │
│  └─ offset   (8 字节 LE)      │  ← 数据在 block 链中的偏移
├───────────────────────────────┤
│  条目 1 ...                    │
└───────────────────────────────┘
```

文件表 block 位于 block 链末尾，由编码器最后写入。解码时先读尾部寻找文件表。

### 3.5 RAW Block (type=0)

数据区直接存储原始字节。用于小文件或 level=1 的 fallback。

### 3.6 LZ77_HUFF Block (type=1)

```
┌───────────────────────────────┐
│  字面量 Huffman 码表          │
│  ├─ 码表长度 (2 字节 LE)      │
│  └─ 码表数据 (变长)           │
├───────────────────────────────┤
│  匹配长度 Huffman 码表        │
│  ├─ 码表长度 (2 字节 LE)      │
│  └─ 码表数据 (变长)           │
├───────────────────────────────┤
│  匹配偏移 Huffman 码表        │  (level 4+)
│  ├─ 码表长度 (2 字节 LE)      │
│  └─ 码表数据 (变长)           │
├───────────────────────────────┤
│  经 Huffman 编码的 LZ77 序列  │
│  ├─ 编码位流大小 (4 字节 LE)  │
│  └─ 编码位流 (变长)           │
└───────────────────────────────┘
```

LZ77 序列的每个元素编码为：
- **字面量**：对应字面量 Huffman 码
- **匹配**：偏移 Huffman 码 + 长度 Huffman 码

### 3.7 SOLID_START / SOLID_NEXT Block (type=2/3)

SOLID_START 包含完整的 LZ77 字典初始化数据和文件数据。SOLID_NEXT 在前一个 block 的字典基础上继续编解码。

```
SOLID_START:
┌─────────────────────────────────┐
│  文件索引 (4 字节 LE)           │
├─────────────────────────────────┤
│  LZ77 压缩数据流                │
└─────────────────────────────────┘

SOLID_NEXT:
┌─────────────────────────────────┐
│  文件索引 (4 字节 LE)           │
├─────────────────────────────────┤
│  字典快照/SHA256 (32 字节)     │  ← 用于一致性校验
├─────────────────────────────────┤
│  LZ77 压缩数据流                │
└─────────────────────────────────┘
```

### 3.8 ENCRYPTED Block (type=4)

```
┌─────────────────────────────────┐
│  Nonce (12 字节)               │  ← ChaCha20 nonce
├─────────────────────────────────┤
│  加密后的内层 block 数据        │
│  (加密前是一个完整 block)       │
└─────────────────────────────────┘
```

内层 block 解密后为另一个完整 block（type 0~3），实现"透明加密"。

### 3.9 SIGNED Block (type=5)

```
┌─────────────────────────────────┐
│  公钥 (32 字节)                 │  ← Ed25519 public key
├─────────────────────────────────┤
│  签名 (64 字节)                 │  ← Ed25519 signature
├─────────────────────────────────┤
│  签名范围描述                   │
│  ├─ block_count (2 字节 LE)    │
│  └─ block_indices (变长)       │  ← 签名覆盖的 block 索引
├─────────────────────────────────┤
│  签名算法标志 (1 字节)          │  ← 0 = Ed25519
└─────────────────────────────────┘
```

签名覆盖的数据为：Magic + Header + 签名 block 之前的所有 block 数据 + 签名 block 中公钥 + 签名范围描述 + 签名算法标志。

---

## 4. 模块职责

### 4.1 `mz_core.c` — 容器核心

负责 MZv2 容器格式的读写：

- 解析/生成 Magic + Header
- Block 链顺序读写（流式处理）
- Block 头部序列化/反序列化
- 文件表的序列化/反序列化
- 文件表 block 的定位（从尾部搜索）
- Block 链的索引构建
- 提供内部迭代器接口供上层调用

**不依赖**加密或压缩模块，只处理容器骨架。

输出接口：
```c
int mz_write_header(uint8_t *buf, int level, uint16_t flags);
int mz_read_header(const uint8_t *buf, int *level, uint16_t *flags);
int mz_write_block(uint8_t *buf, size_t *off, int type, const void *data, size_t size);
int mz_read_block(const uint8_t *buf, size_t len, size_t *off, int *type, const uint8_t **data, size_t *size);
```

### 4.2 `mz_lz77.c` — LZ77 匹配与编解码

当前代码实现了一个简单的 4KB 窗口 LZ77（hash 链匹配），v2 改进版：

**数据结构**：
- 三级 hash 表（level 4-6）：`head[]` + `chain[]` + `next[]`
- 后缀数组近似（level 7-9）：基于 bucket sort 的 SA 构建
- 循环字典窗口：大小随 level 变化（64KB ~ 1MB）

**匹配策略**：
- Level 1-3：hash 链，最大 256 次比较
- Level 4-6：三级 hash + lazy matching
- Level 7-9：suffix array + optimal parsing（基于代价模型）

**编码 API**：
```c
/* 初始化 LZ77 编码器 */
struct mz_lz77_enc *mz_lz77_enc_new(int level, size_t winsize);
void mz_lz77_enc_free(struct mz_lz77_enc *e);

/* 编码一块数据，输出 (match, literal) 序列 */
int mz_lz77_encode(struct mz_lz77_enc *e,
                   const uint8_t *in, size_t inlen,
                   uint8_t *out, size_t *outlen);

/* 解码 */
int mz_lz77_decode(const uint8_t *in, size_t inlen,
                   uint8_t *out, size_t outlen,
                   size_t winsize);
```

### 4.3 `mz_huf.c` — Huffman 编码/解码

用于 level 4-6 的熵编码层：

- 经典 Huffman 树构建（基于频率统计）
- 码表压缩：使用规范 Huffman 编码（canonical Huffman），只存储码长
- 编码：位流打包（MSB first）
- 解码：基于码长查表（lookup table 加速）
- 支持动态码表（每 block 独立统计）

**码表格式**（规范 Huffman）：
```
┌───────────────────────┐
│  符号数 (2 字节 LE)   │
├───────────────────────┤
│  符号列表 (符号数 字节)│
├───────────────────────┤
│  码长列表 (符号数 字节)│
└───────────────────────┘
```

### 4.4 `mz_ans.c` — tANS 编码/解码

用于 level 7-9 的高级熵编码：

- 标准 tANS（table-based Asymmetric Numeral Systems）
- 状态表大小：1024（level 7）、2048（level 8）、4096（level 9）
- 编码表构建基于符号频率排序
- 解码表直接查表（单步 decode）
- 位流交错：按 16-bit 批次输出/输入

**tANS 优势**：比 Huffman 更接近熵极限，且解码速度高于算术编码。

### 4.5 `mz_crypt.c` — 加密/签名封装

封装密码学操作：

```c
/* ChaCha20 对称加密 */
void mz_chacha20_xor(uint8_t *out, const uint8_t *in, size_t len,
                     const uint8_t key[32], const uint8_t nonce[12]);

/* Ed25519 签名封装 */
int mz_sign(const uint8_t *data, size_t len,
            const uint8_t sk[64], uint8_t sig[64],
            uint8_t pk[32]);
int mz_verify(const uint8_t *data, size_t len,
              const uint8_t pk[32], const uint8_t sig[64]);

/* 密钥生成 */
int mz_generate_keypair(uint8_t sk[64], uint8_t pk[32]);
```

实现细节：
- ChaCha20 使用 20 轮标准实现
- Ed25519 复用 `projects/meuos-sysroot/src/libmsys/ed25519.c`
- 所有密钥/签名长度固定，无堆分配

### 4.6 `mz_solid.c` — 固实压缩上下文管理

管理跨文件的 LZ77 字典持久化：

```c
struct mz_solid_ctx {
    struct mz_lz77_enc *encoder;   /* LZ77 编码器（共享字典） */
    size_t win_size;                /* 字典窗口大小 */
    uint64_t total_input;           /* 累计输入字节数 */
    int file_count;                 /* 当前固实流中文件数 */
    uint8_t dict_hash[32];          /* 字典内容的 SHA256 快照 */
};
```

- `mz_solid_open()` — 初始化固实上下文
- `mz_solid_close()` — 输出最终字典快照
- `mz_solid_append()` — 追加文件到固实流
- `mz_solid_next()` — 准备下一个文件（保留字典）

### 4.7 `mz_main.c` — 入口与调度

实现公共 API（在 `mz.h` 中声明）：

```c
int mz_compress(const void *in, size_t il, void **r, size_t *rl,
                int codec, int level);
int mz_decompress(const void *in, size_t il, void **r, size_t *rl,
                  int codec);
size_t mz_max_compressed_size(size_t il, int codec);
const char *mz_strerror(int e);
```

Level 调度的核心逻辑：

```
mz_compress() 入口
  │
  ├─ level 1-3
  │   └─ mz_lz77_encode() → RAW block（或 LZ77_HUFF block
  │       without Huffman, i.e. 位打包）
  │
  ├─ level 4-6
  │   └─ mz_lz77_encode() → mz_huf_encode() → LZ77_HUFF block
  │
  └─ level 7-9
      └─ mz_lz77_encode(optimal) → mz_ans_encode() → LZ77_HUFF block
```

---

## 4. meuos-compress 统一算法

### 4.1 设计思想

meuos-compress 是一个**渐进式融合算法引擎**，不是 "LZ77 + 可选熵编码" 的拼凑。

```
Level 1-3:   快速模式
             字节级 hash 匹配 + 简单位流打包
             参考 LZ4 的极简管线

Level 4-6:   均衡模式  
             hash chain 深度匹配 + Huffman 熵编码
             参考 zstd 的 lazy matching 策略

Level 7-9:   极限模式
             最优解析 + tANS 熵编码 + 自动模式切换
             引擎内部根据数据特征选择策略
```

### 4.2 引擎内部决策

同一份代码在内部根据 level 和实时数据特征做渐进叠加：

```c
// 统一算法引擎伪代码
int meuos_compress_block(int level, ...) {
    // Layer 1: 匹配查找 (共用入口，深度不同)
    int depth = level * 512;
    if (level <= 3)  find_matches_fast(depth);
    else             find_matches_deep(depth);
    
    // Layer 2: 熵编码 (引擎自动选择)
    if (level <= 3 || bits_per_byte < 1.1)
        pack_literals();        // 简单位打包
    else if (level <= 6)
        huffman_encode();       // 经典 Huffman
    else
        tans_encode();          // tANS，更高压缩比
    
    // Layer 3: 增值特性
    if (flags & ENCRYPT) chacha20_encrypt();
    if (flags & SIGNED)  ed25519_sign();
}
```

### 4.3 演进路径

| 阶段 | 内容 | 参考 | 收益 |
|------|------|------|------|
| Phase A | LZ77 + Huffman + ChaCha20 | LZ4 + zstd | 基线已实现 |
| Phase B | tANS + 深度匹配 | zstd FSE | +20% |
| Phase C | 固实压缩 + 字典 | zstd --train | -30% |
| Phase D | LZ4 快速路径 | LZ4 format | 100x 速度 |
| Phase E | BCJ 预处理器 | 7z BCJ2 | +30% ELF |

### 4.4 格式兼容设计

既然引擎内部融合了多种策略，**解压时自动兼容**就成为一个自然的能力。

```
mz_decompress(data, len, &out, &outlen, codec)
                      │
                      ▼
          自动嗅探 magic 前 2-4 字节
                      │
          ┌─────┬─────┼──────┬──────┐
          ▼     ▼     ▼      ▼      ▼
       "MZ"  "mZ"  "MZv2"  0x28   raw
       (v1)  (v2)  (MZv2)  (LZ4)  LZ77
         │     │      │      │      │
         ▼     ▼      ▼      ▼      ▼
      engine adapts to detected format
```

**兼容原则**：
- `mz_decompress()` 当 `codec == 0` 时启用**自动嗅探模式**，从数据前几个字节推断格式
- 识别范围：MeuOS MZv1、MZv2、及引擎内部支持的社区算法格式
- 压缩时始终输出 MZv2 格式（统一）
- 解压时引擎自动适配，对调用方透明

**支持的解压兼容格式**：

| 格式 | 检测 magic | 说明 |
|------|-----------|------|
| MZv1 | `"MZ\0"` 或 `"MZ"` + version=1 | 旧版 LZ77（已实现） |
| MZv2 | `"MZv2"` | 新版容器格式（已实现） |
| raw LZ77 | 无 magic，需显式指定 | 裸 LZ77 流（level 1 输出） |

这样就实现了"**一个引擎，格式自适应**"——无需用户操心该用什么 codec，`codec=0` 就自动全兼容。
|------|-----------|----------|--------|------|
| 1    | 64KB      | hash 链 (128) | 位打包 | 最快，适合实时 |
| 2    | 64KB      | hash 链 (256) | 位打包 | 快速 |
| 3    | 64KB      | hash 链 (512) | 位打包 | 平衡 |
| 4    | 256KB     | 三级 hash      | Huffman | 兼顾速度与压缩率 |
| 5    | 256KB     | 三级 hash + lazy | Huffman | 中等压缩 |
| 6    | 256KB     | 三级 hash + lazy+ | Huffman | 高压缩 |
| 7    | 1MB       | SA + greedy    | tANS-1024 | 极高压缩 |
| 8    | 1MB       | SA + optimal   | tANS-2048 | 接近极限 |
| 9    | 1MB       | SA + optimal+  | tANS-4096 | 最大压缩 |

**层级演进的三个维度**：
1. **窗口越大** → 发现更远距离的重复
2. **匹配越精确** → lazy matching → optimal parsing 减少冗余匹配
3. **熵编码越优** → 位打包 → Huffman → tANS

---

## 6. 固实压缩（Solid）

### 6.1 原理

固实压缩将多个文件视为一个连续数据流，共享 LZ77 字典。后一个文件可以引用前一个文件中的匹配，显著提升小文件集合的压缩率。

### 6.2 使用方式

```c
/* 单次调用压缩多个文件 */
int mz_compress_files(mz_file_entry *files, int nfiles,
                      void **out, size_t *outlen, int level);

/* 或通过文件表构建 */
struct mz_file_table *table = mz_file_table_new();
mz_file_table_add(table, "a.txt", 0644, data_a, len_a);
mz_file_table_add(table, "b.txt", 0644, data_b, len_b);
mz_compress_solid(table, out, outlen, 6);
```

### 6.3 文件表

```c
typedef struct {
    const char *name;     /* 文件名（不包含路径分隔符） */
    uint32_t uid;         /* 用户 ID */
    uint32_t gid;         /* 组 ID */
    uint16_t mode;        /* Unix 权限位 */
    const uint8_t *data;  /* 文件数据 */
    uint64_t size;        /* 文件大小 */
} mz_file_entry;
```

文件表 block 记录每个文件在解压后数据流中的偏移量，使得随机访问单个文件成为可能。

---

## 7. 加密方案

### 7.1 架构

```
对称加密（ChaCha20）：
  ┌──────────┐     ┌──────────────┐     ┌───────────────┐
  │ 明文数据  │────→│ ChaCha20 XOR │────→│ ENCRYPTED block│
  │ (block)  │     │ key=32B      │     │ (nonce + 密文) │
  └──────────┘     │ nonce=12B    │     └───────────────┘
                   └──────────────┘

非对称签名（Ed25519）：
  ┌──────────────┐    ┌──────────┐    ┌──────────────┐
  │ Magic+Header │    │ 私钥 sk  │    │ SIGNED block │
  │ + 所有 block │───→│ Ed25519  │───→│ (pk + 签名)  │
  └──────────────┘    │ sign()   │    └──────────────┘
                      └──────────┘
```

### 7.2 ChaCha20

- 标准 ChaCha20（20 轮）
- 密钥：32 字节（256 位）
- Nonce：12 字节（96 位），每个 ENCRYPTED block 使用独立 nonce
- Nonce 生成：使用 12 字节递增计数器 + 随机初始化
- 加密模式：XOR 流（CTR 模式等价）
- 加密粒度为 block 级别，每个 ENCRYPTED block 包含一个完整的内层 block

### 7.3 Ed25519

- Ed25519 实现来自 `projects/meuos-sysroot/src/libmsys/ed25519.c`
- 公私钥对：种子 32B → 私钥 64B + 公钥 32B
- 签名 64 字节
- SIGNED block 可位于文件末尾，或作为独立 block 追加

### 7.4 典型加密流程

```c
/* 压缩并加密 */
uint8_t key[32] = {...};  // 用户提供的对称密钥
uint8_t sk[64];           // Ed25519 私钥
uint8_t pk[32];           // Ed25519 公钥

/* 1. 先压缩为普通 MZv2 流 */
mz_compress(data, len, &mz_stream, &mz_len, 1, 6);

/* 2. 压缩流整体加密为 ENCRYPTED block（或逐 block 加密）*/
uint8_t nonce[12];
mz_generate_nonce(nonce);
mz_chacha20_xor(encrypted, mz_stream, mz_len, key, nonce);

/* 3. 写入 SIGNED block */
ed25519_sign(sk, mz_stream, mz_len, sig);
mz_write_signed_block(out, pk, sig, ...);
```

---

## 8. msys 集成

### 8.1 角色定位

mz 库作为 msys 虚拟文件系统的可插拔 codec 层。`.msys` 文件可以在内部使用 mz 压缩流代替原始数据。

### 8.2 集成接口

msys 通过以下接口调用 mz：

```c
/* msys → mz codec 适配层 */
struct mz_codec {
    int (*compress)(const void *in, size_t inlen,
                    void **out, size_t *outlen, int level);
    int (*decompress)(const void *in, size_t inlen,
                      void **out, size_t *outlen);
    const char *name;
};
```

msys 在挂载 `.msys` 文件时检测 block 头部是否匹配 `MZv2` magic。若匹配，则使用 mz codec 解压；若不匹配，视为原始数据直接读取。

### 8.3 直接解压支持

`.msys` 文件可直接通过 mz 命令行工具解压：

```
mz x archive.msys       # 检测并解压 msys 中的 mz 压缩数据
mz x file.mz            # 解压普通 mz 文件
mz c -o archive.mz dir/ # 压缩目录
```

### 8.4 文件元数据映射

| mz 文件表字段 | msys inode 映射 |
|---------------|-----------------|
| name          | dirent.name     |
| uid           | inode.uid       |
| gid           | inode.gid       |
| mode          | inode.mode      |
| size          | inode.size      |

---

## 9. 数据流

### 9.1 压缩流程

```
用户数据
  │
  ├─ [多文件] → mz_solid_append() 逐个文件追加
  │               └─ 共享 LZ77 字典
  │
  ├─ [单文件] → 直接送入 LZ77 编码器
  │
  └─ level 调度
       │
       ├─ level 1-3:  LZ77(e) → 位打包 → LZ77_HUFF/RAW block
       ├─ level 4-6:  LZ77(e) → Huffman → LZ77_HUFF block
       └─ level 7-9:  LZ77(o) → tANS    → LZ77_HUFF block
                          │
                          └─ [可选 ChaCha20] → ENCRYPTED block
                               then [可选 Ed25519] → SIGNED block
  │
  └─ File Table Block → 写入 block 链末尾
```

### 9.2 解压流程

```
MZv2 输入流
  │
  ├─ 读取 Magic + Header → 获取 level, flags
  │
  ├─ 从尾部定位 File Table Block → 解析文件列表
  │
  ├─ 遍历 Block 链（按文件偏移读取）
  │    │
  │    ├─ ENCRYPTED → ChaCha20 解密 → 内层 block
  │    ├─ SIGNED    → Ed25519 验证 → 继续
  │    ├─ RAW       → 直接拷贝
  │    ├─ LZ77_HUFF → Huffman/tANS → LZ77 解码
  │    ├─ SOLID_START → 重置字典 → LZ77 解码
  │    └─ SOLID_NEXT  → 继承字典 → LZ77 解码
  │
  └─ 按文件表将解压数据分发给各文件
```

---

## 附录 A：错误码

| 宏               | 值  | 说明 |
|------------------|-----|------|
| MZ_OK            | 0   | 成功（或输出长度） |
| MZ_ERR_MEMORY    | -1  | 内存分配失败 |
| MZ_ERR_DATA      | -2  | 数据损坏或格式错误 |
| MZ_ERR_PARAM     | -3  | 参数错误 |
| MZ_ERR_STREAM    | -4  | 流处理错误 |
| MZ_ERR_CODEC     | -5  | 不支持的 codec |
| MZ_ERR_CRYPT     | -6  | 加解密/签名验证失败 |

## 附录 B：版本历史

| 版本 | 说明 |
|------|------|
| v1   | 初始 LZ77 实现，4KB 窗口，MZ 格式 |
| v2   | 多级压缩、Huffman/tANS、ChaCha20 + Ed25519、固实压缩 |
