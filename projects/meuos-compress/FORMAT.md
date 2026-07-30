# MxA (MeuOS Archive) 格式规范

> MxA 是 MeuOS 自有容器格式，v1 实现。完全脱离 zlib/7z 风格。

---

## 一、布局总览

```
 偏移         大小  字段
 ─────────────────────────────────
      0       16   Archive Header
     16        ?   File Data Blocks (连续存放，无间隙)
      ?        8   CD Header (MxCD)
      ?      48+   N × File Entries
      ?       40   CD Footer (MxCF)
      ?     0|64   Ed25519 Signature (可选)
 ─────────────────────────────────
```

文件末尾的 CD（Central Directory）是**可变长索引区域**。解析时从末尾向前扫描。

---

## 二、字段详解

### Archive Header (16B @ offset 0)

| 偏移 | 大小 | 类型 | 字段 | 说明 |
|------|------|------|------|------|
| 0 | 4 | uint32 LE | magic | `"MxA1"` (= 0x3141784D) |
| 4 | 2 | uint16 LE | hdr_flags | MXA_FLAG_* |
| 6 | 1 | uint8 | version_min | 最低兼容版本 (v1: 1) |
| 7 | 1 | uint8 | version_cur | 当前版本 (v1: 1) |
| 8 | 1 | uint8 | align_shift | 数据对齐 (0=无) |
| 9 | 7 | uint8[7] | reserved | 全零 |

**标志位 (hdr_flags):**
- `MXA_FLAG_SIGNED` (0x0001) — 容器有 Ed25519 签名
- `MXA_FLAG_ENCRYPTED` (0x0002) — 数据区已加密

### CD Header (8B)

| 偏移 | 大小 | 类型 | 字段 |
|------|------|------|------|
| 0 | 4 | uint32 LE | cd_magic = `"MxCD"` (0x4443784D) |
| 4 | 4 | uint32 LE | num_files |

### File Entry (48B + filename)

每个文件一条记录。

| 偏移 | 大小 | 类型 | 字段 |
|------|------|------|------|
| 0 | 2 | uint16 LE | name_len |
| 2 | 2 | uint16 LE | mode (Unix) |
| 4 | 4 | uint32 LE | uid |
| 8 | 4 | uint32 LE | gid |
| 12 | 8 | uint64 LE | mtime |
| 20 | 8 | uint64 LE | offset (数据偏移) |
| 28 | 8 | uint64 LE | size (解压后) |
| 36 | 8 | uint64 LE | csize (压缩后) |
| 44 | 1 | uint8 | codec (MXA_CODEC_*) |
| 45 | 3 | uint8[3] | reserved |
| 48 | name_len | uint8[] | name (UTF-8, 无 `\0` 终止) |

**Codec 值:**
- `MXA_CODEC_STORED` (0) — 原始不压缩
- `MXA_CODEC_MEUOS` (1) — meuos-compress 统一算法

### CD Footer (40B)

| 偏移 | 大小 | 类型 | 字段 |
|------|------|------|------|
| 0 | 4 | uint32 LE | footer_magic = `"MxCF"` (0x4643784D) |
| 4 | 4 | uint32 LE | total_files (冗余) |
| 8 | 8 | uint64 LE | cd_start (CD 在归档中的偏移) |
| 16 | 8 | uint64 LE | total_uncompressed |
| 24 | 8 | uint64 LE | total_compressed |
| 32 | 4 | uint32 LE | cd_checksum (CRC32 of CD entries) |
| 36 | 4 | uint32 LE | hdr_checksum (CRC32 of Archive Header) |

### Signature (0 或 64B)

仅当 `hdr_flags & MXA_FLAG_SIGNED` 时存在。内容：
- 签名算法: Ed25519
- 签名内容: Hash(Archive Header + CD Footer 前 36 字节)
- 签名长度: 64 字节固定

---

## 三、流式解析流程

1. 从 EOF 向前扫描：
   - 检查最后 64 字节是否是 Ed25519 签名 → 确定签名起始
   - 从签名前 40 字节读 CD Footer → 校验 `footer_magic`
2. 从 CD Footer 读 `cd_start` → 读 CD Header → 校验 `cd_magic`
3. 读 File Entries → 校验 CRC32
4. 读 Archive Header → 校验 magic + CRC32

共 **3 次 seek + 3 次 read**，O(N) 内存，不扫描数据块。

---

## 四、对比 MZv2

| 特性 | MZv2 | MxA v1 |
|------|------|--------|
| 随机访问 | ❌ 扫描全部块 | ✅ O(1) CD 查找 |
| 文件大小 | 16MB/块 | ✅ uint64 无上限 |
| 头开销 | 12B + 块链 | 16B + 48B/文件 + 文件名 |
| 流式写入 | ✅ | ✅ (CD 最后写) |
| 加密 | 块级 | ✅ 容器级 ChaCha20 |
| 签名 | 块级 | ✅ 容器级 Ed25519 |
| 格式版本 | magic 编码 | ✅ ver_min/ver_cur |

---

## 五、MXA_CODEC_MEUOS 压缩格式

MxA 使用 `MXA_CODEC_MEUOS` 作为默认压缩编码。该编码的内部格式为：

```
[entropy_type:1] [entropy_coded_data:?]
```

**entropy_type:**
- `0`: Raw LZ77 (mZ 格式), 直接解码
- `1`: LZ77 + Huffman, 需先解 Huffman 再解 LZ77
- `2`: LZ77 + tANS, 需先解 tANS 再解 LZ77

**Level 对应:**
- lv1-3: LZ77 only (type=0)
- lv4-6: LZ77 + Huffman (type=1, if beneficial)
- lv7-9: LZ77 + tANS (type=2, if beneficial)
