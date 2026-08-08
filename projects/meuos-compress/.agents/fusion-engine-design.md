# 自研融合压缩引擎设计文档

> 2026-08-08 — compress-worker

---

## 1. 问题陈述

当前 `mz_combo.c` 是"管道式"设计：

```
输入 → LZ77 匹配 → 裸 LZ77 字节流 → Huffman/tANS 熵编码 → 输出
```

每次压缩经历两遍：第一遍产生中间裸 LZ77 流，第二遍对裸流再编码。这导致：
- 两倍内存（中间缓冲区）
- LZ77 token 的位效率未充分利用（字面量只用 7-bit 编码、偏移固定 16-bit）
- Entropy 编码器无法感知 LZ77 匹配的语义特征
- 代码结构上是"先 A 后 B"，不是融合

**正确的融合引擎**（参考 mcc 融合 cproc+QBE → MIR 的路线）：

```
输入 → 统一匹配+熵编码引擎 → 直接输出熵编码位流
      （一次遍历，无中间格式）
```

---

## 2. 设计方案

### 2.1 架构概述

新建 `mz_fusion.c`，替代 `mz_combo.c` 的角色。

```
mz_compress_meuos()
  │
  ├─ 数据特征分析（熵估计/周期性/可压缩性）
  │   └─ 决定匹配策略 + 熵编码类型
  │
  ├─ 一次遍历循环：
  │   while (ip < inlen) {
  │       1. 查找最佳匹配（hash chain / lazy / optimal）
  │       2. 直接输出熵编码的 token：
  │          - 字面量 → 字面量熵编码器（Huffman/tANS/raw）
  │          - 匹配 (offset, length) → 匹配熵编码器
  │   }
  │
  └─ 输出：熵编码位流 + 码表头
```

### 2.2 熵编码输出格式（替代当前 LZ77 raw token 格式）

当前 raw 格式：
```
字面量: 0bbbbbbb (7-bit) 或 0xFF + byte (转义)
匹配:   1ooooooo mmmmmmmm mlllllll (3 字节, 16-bit offset + 7-bit length)
```

融合格式（每 token 直接熵编码到位流）：
```
Header:   [entropy_type:1B] [Huffman/tANS 码表]
Data:     [熵编码位流]
```

熵编码器接收三种符号：
- **字面量符号** (0-255): 直接字符
- **匹配长度符号** (0-257): 长度基值（编码为长度类 + 额外位，类似 zstd 的 `match_len`)
- **匹配偏移符号** (0-31): 偏移基值（编码为偏移类 + 额外位）

三种符号各用独立的 Huffman/tANS 码表（或共享，取决于配置）。

### 2.3 三种模式

| 模式 | 编码方式 | 适合场景 |
|------|---------|---------|
| RAW bypass | 字面量直接存储，匹配不编码 | level 1-2, 不可压缩数据 |
| Huffman | 三张规范 Huffman 码表（字面量/长度/偏移） | level 3-6, 一般文本 |
| tANS | 三张 tANS 码表 | level 7-9, 极高压缩 |

RAW bypass 用现有 `mz_lz77.c` 直接输出，不需要新实现。
Huffman 和 tANS 模式需要新的融合编码器。

### 2.4 解压对应

融合引擎的解压器：
```
输入 → 读 entropy_type + 码表 → 逐 token 熵解码 → 还原 LZ77 匹配 → 输出
```

解码器只需一次遍历，不需要两遍。

---

## 3. 实施步骤

### Phase 1: 数据结构定义（影响范围：`mz.h` + 新 `mz_fusion.c`）

```c
/* 融合引擎编码上下文 */
struct mz_fusion_enc {
    /* 匹配器 */
    struct mz_state *matcher;        /* 复用现有 LZ77 匹配器 */
    int level;
    int etype;                       /* RAW / HUFF / TANS */

    /* 熵编码器（选其一，取决于 etype） */
    union {
        struct { /* RAW: 字节流写入器 */ } raw;
        struct { /* HUFF: 三张 Huffman 码表构建器 + 位流写入器 */ } huff;
        struct { /* TANS: 三张 tANS 码表构建器 + 位流写入器 */ } tans;
    } enc;
};
```

### Phase 2: Huffman 融合编码器（影响范围：`mz_fusion.c`）

- 复用 `mz_huf.c` 的树构建 + 规范码表生成
- 新增"一次性遍历"API：`mz_fusion_huff_encode(in, len, out, outlen, level)`
- 内部循环：一边做 hash 链匹配，一边用 bit writer 输出 Huffman 码

**与 mz_huf.c 的关系**：`mz_huf.c` 保留作为纯 Huffman 编解码工具。`mz_fusion.c` 是调用者。

### Phase 3: tANS 融合编码器（影响范围：`mz_fusion.c`）

- 复用 `mz_ans.c` 的表构建
- 新增"一次性遍历"API
- 内部循环：匹配+熵编码，直接输出 tANS 状态流

### Phase 4: 统一入口替换（影响范围：`mz_combo.c` → 废弃）

- `mz_compress_meuos()` 改为调用 `mz_fusion` 引擎
- `mz_combo.c` 标记为废弃，保留一段时间做回归对照

### Phase 5: 容器格式适配（影响范围：`mz_core.c`）

- 当前 `LZ77_HUFF` block type 仍然可用，但内部格式变为融合格式
- 新增 block type 枚举值（可选）：`MZ_BLOCK_FUSION` (type 6)
- 向后兼容：解压器自动识别融合格式 vs 管道式格式

---

## 4. 影响范围分析

| 组件 | 影响 | 风险 |
|------|------|------|
| `mz.h` | 新增 2-3 个 API 声明 | 低 |
| `mz_fusion.c`（新） | 全部新代码 | 中（新算法复杂性） |
| `mz_combo.c` | 逐渐废弃 | 低 |
| `mz_lz77.c` | 复用匹配器，不受影响 | 低 |
| `mz_huf.c` | 复用工具函数 | 低 |
| `mz_ans.c` | 复用工具函数 | 低 |
| `mz_main.c` | 路由到融合引擎 | 低 |
| `mz_core.c` | 可能需要新 block type | 低 |
| Makefile | 加新源文件 | 低 |
| 测试 | 需增加融合引擎往返测试 | 中 |

**风险**：融合引擎的匹配+熵编码一体化逻辑复杂度较高。建议先用 Huffman 模式实现（较简单，验证思路），再扩展到 tANS。

---

## 5. 与 msys v3 的关系

融合引擎完成后，`libmz.a` 将提供：
- `mz_compress(in, len, &out, &outlen, MZ_CODEC_MEUOS, level)` — 使用融合引擎
- `mz_decompress(in, len, &out, &outlen, MZ_CODEC_MEUOS)` — 自动识别融合格式

msys v3 集成时，`msys_fopen` 的解压调度从：
```c
// 当前：dlopen zlib/zstd
if (flags & MSYS_F_ZLIB) decompress_zlib(...);
if (flags & MSYS_F_ZSTD) decompress_zstd(...);
```
改为：
```c
// 新：直接链接 libmz
if (flags & MSYS_F_MZ) mz_decompress(..., MZ_CODEC_MEUOS);
```

这意味着 msys 需要新增 `MSYS_F_MZ` flag，在 mkmsys 打包时自动选择 mz codec。

---

## 6. 方案选项与取舍

### 方案 A（推荐）：逐 token 熵编码
每产生一个字面量或匹配，立即编码到位流。最简单，与现有 LZ77 循环集成最自然。

**优点**：实现简单，代码改动小，复用现有匹配器
**缺点**：字面量频次统计需要滚动更新（前向窗口方案稍复杂）

### 方案 B：批量熵编码
先收集一批 token 到 seqStore（类似 zstd），统一统计频次后批量编码。

**优点**：压缩比略高（全局频次更准）
**缺点**：需要中间缓冲区，代码复杂度高

**选择方案 A** — 因为我们的目标不是逐字节极致压缩比，而是"融合设计，不走两遍"。方案 A 已经足够；未来可以演进到方案 B。

---

## 7. 实施路线图

```
Day 1:  mz_fusion.c 框架 + Huffman 融合编码器实现 + 基础测试
Day 2:  tANS 融合编码器实现 + 全部 level 测试
Day 3:  废弃 mz_combo.c + make check 全线通过 + msys v3 集成设计
```

每个阶段完成后提交+make check 验证。禁止"管道式"代码进入融合引擎。