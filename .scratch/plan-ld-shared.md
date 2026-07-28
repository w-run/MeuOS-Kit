# ld-shared: mt/ld `-shared` ET_DYN 输出

## 目标
mt/ld 当前只做 ET_EXEC 静态链接。添加 `-shared` 支持输出 ET_DYN（共享库）。

## 设计原则
- 不照搬 GNU ld 的繁琐特性。只实现 MeuOS 需要的动态链接地基
- ELF 常量已在 `elf.h` 定义，readelf/nm 已支持解析动态节区
- 后续 ld.so 和 dlopen 依赖此基础

## 改动清单

### 1. API 扩展 (`include/mt/ld.h`)
```c
// 新增标志结构体
struct mt_ld_options {
    const char *output;
    const char *entry;
    const char *soname;   // DT_SONAME
    int shared;           // 1=ET_DYN, 0=ET_EXEC
};
int mt_ld_link_opts(const struct mt_ld_options *opts,
                    const char *const *inputs, size_t input_count,
                    const char *target, const char **error);
// 保留旧 API 作为 wrapper（shared=0）
```

### 2. CLI 解析 (`src/ld/main.c`)
- 新增 `--shared` / `-shared` 解析
- 新增 `--soname=<name>` 解析
- shared 模式下不强制要求 `-e entry`

### 3. 核心链接器 (`src/ld/link.c`)

**A. `struct ld_context` 新增字段**
- `int shared` — 输出类型标志
- `soname` — 库名
- `dynsym`, `dynstr`, `hash`, `dynamic`, `interp`, `rela_dyn`, `rela_plt`, `plt`, `got_plt` — 动态节区的 group 句柄

**B. `collect_sections()` 扩展**
- 新增动态节区收集/生成路径（shared 模式才触发）

**C. 新增 `build_dynsym()` 函数**
- 从全局符号表选择导出符号（global + defined + 非本地）
- 生成 `.dynsym`（SHT_DYNSYM，64 bytes/entry）
- 生成 `.dynstr`（SHT_STRTAB）
- 生成 `.hash`（SHT_HASH，SysV 32-bit 哈希表）

**D. 新增 `build_dynamic()` 函数**
- 生成 `.dynamic` 节区（SHT_DYNAMIC）
- DT_SYMTAB / DT_SYMENT → 指向 `.dynsym`
- DT_STRTAB / DT_STRSZ → 指向 `.dynstr`
- DT_HASH → 指向 `.hash`
- DT_SONAME（如果提供了 soname）
- DT_RELA / DT_RELASZ → 指向 `.rela.dyn`
- DT_JMPREL / DT_PLTRELSZ → 指向 `.rela.plt`
- DT_PLTGOT → 指向 `.got.plt`
- DT_INIT / DT_FINI（如果有）
- DT_FLAGS（DF_BIND_NOW 等）

**E. `write_executable()` → 改为 `write_output()`**
- `e_type`: 硬编码 `2` → `shared ? MT_ET_DYN : MT_ET_EXEC`
- `e_entry`: 共享库可以用 0（或初始化函数地址）
- `e_phnum`: 硬编码 `2` → 动态计算
- **新增 PT_DYNAMIC** 程序头
- **新增 PT_PHDR** 程序头（ET_DYN 需要）
- **可选 PT_INTERP**（PIE 需要）
- **PT_LOAD 拆分**: 当前单个 RWX 段 → 至少 RX 和 RW 两个段

**F. 动态重定位**
- `.rela.dyn`: 收集需要加载时重定位的条目
  - `R_X86_64_RELATIVE`: 绝对地址引用（当前已解析的值作为 addend）
  - `R_X86_64_GLOB_DAT`: GOT 条目
- `.rela.plt`: PLT 条目的跳转槽重定位
  - `R_X86_64_JUMP_SLOT`

**G. PLT/GOT.PLT 生成**
- `.plt`: x86_64 标准 PLT 存根（16 bytes/entry + 特殊第一项）
- `.got.plt`: 初始值指向 PLT 解析器

### 4. 测试验证
```sh
# 编译两个 .o 文件
mcc -fPIC -c -o foo.o foo.c
mcc -fPIC -c -o bar.o bar.c
# mt/ld 链接为共享库
ld -shared -o libfoo.so foo.o bar.o
# 验证
readelf -h libfoo.so | grep ET_DYN
readelf -d libfoo.so | grep -q 'NEEDED\|SONAME'
readelf -s libfoo.so --dynamic 2>/dev/null || readelf --dyn-syms libfoo.so
```

## 关键文件

| 文件 | 改动 |
|------|------|
| `include/mt/ld.h` | 新增 `mt_ld_options` + `mt_ld_link_opts()` |
| `src/ld/main.c` | 新增 `-shared`, `--soname` CLI |
| `src/ld/link.c` | 核心改动：e_type/PHDR/动态节区/重定位/PLT |
| `include/mt/elf.h` | 确认常量完备（当前已完备） |
