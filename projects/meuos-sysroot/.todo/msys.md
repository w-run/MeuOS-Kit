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
```

## 任务清单

### Phase 1A — libmsys 核心库 ✅

- [x] `include/mt/msys.h`：声明 `msys_open()`, `msys_close()`, `msys_read()`, `msys_search()`
- [x] `src/libmsys/msys.c`：mmap → 解析 header → 索引二分查找 → 返回数据指针
- [x] 集成到 Makefile：产出 `build/libmsys.a`

### Phase 1B — mkmsys 打包工具 ✅

- [x] `src/mkmsys/main.c`：遍历目录 → FNV-1a 哈希 → 排序 → 写入 .msys
- [x] 支持 `-o <output>`、`--list` 列出内容
- [x] 支持 `--arch <name>` 写入元数据键 `meuos_arch`

### Phase 2 — mcc 集成 ✅

- [x] `mcc/src/driver/msys.c`：sysroot 抽象层，检测 `.msys` 后缀走 libmsys 读取
- [x] preprocessor include 搜索支持 .msys
- [x] 未显式传 `--target` 时，从 .msys 提取 `meuos_arch`

### Phase 3 — mt/ld 集成 ⬜ 待启动

- [ ] mt/ld 的 `-L` 参数识别 .msys
- [ ] 从 .msys 索引中读取 .a/.o 参与链接

### Phase 4 — 构建流水线 ⬜ 待启动

- [ ] `make msys`：自动从 sysroot 目录生成 .msys
- [ ] `bootstrap.sh` 阶段产出 .msys

## 验收

```sh
# 打包
mkmsys -o /tmp/aarch64.msys /path/to/sysroot-aarch64
mkmsys --list /tmp/aarch64.msys  # 列出所有路径

# mcc 读头文件
mcc --target=aarch64 --sysroot=/tmp/aarch64.msys -c -o test.o test.c

# 全链路
mcc --sysroot=/tmp/aarch64.msys -o test test.c
```

## 参考

- FNV-1a 哈希算法：https://en.wikipedia.org/wiki/Fowler–Noll–Vo_hash_function
- 现有工具链中的 libelf 共享库模式：`meuos-toolchain/src/libelf/`
