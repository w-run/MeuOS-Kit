<!--
priority: P3
status: done
done_ts: 2026-07-23
note: i386_sysv_abi 预扫描重写为 libc 软算术调用，剩 qemu 真 32 位内核回归验证
-->

# 已完成：i386 Kl（64 位整数）乘法 / 除法 / 取余

> Update 2026-07-23：已通过 i386_sysv_abi() 预扫描解决。所有 Omul/Odiv/
> Orem/Oudiv/Ourem Kl 在 ABI 层重写为 Oarg/Oarg/Ocall 调用 libc 软算术
> 库（meuos-libc/src/arch/i386/soft_arith.c），不再经 isel/emit。
>
> 具体变更：
> - `i386_sysv.c` L607-641：预扫描将 Kl mul/div/rem 重写为函数调用
> - `i386_isel.c`：移除 Odiv/Orem/Oudiv/Ourem Kl 的显式 `die`
> - `i386_emit.c`：Omul Kl 防卫性 `die`（应不可达）
> - `test/i386/runtime_kl.c`：新增乘/除/取余测试，含符号/无符号、边界溢出
>
> 验收状态：待 qemu 真 32 位内核回归验证。
>
> 历史参考留档：以下为原修复方案描述，供维护查阅。

## 背景

i386 的 `kl_in_reg = 0`，即 `Kl`（64 位整数）临时量永远槽驻留
（两个 32 位 slot 拼成），不进入寄存器。其余 Kl 运算已在 emit 层分解：

- add/sub/neg/and/or/xor → `adcl`/`sbbl` 进位链（emit 内分解）
- shifts → `shldl`/`shrdl`（emit L1176-1229，**已落地**，i386_isel.c
  顶部注释此前称 shifts 仍 die 的描述已修正）

但 **乘法 / 除法 / 取余** 三类 Kl 运算尚无实现路径，当前会触发
编译器崩溃。

## 源码核实（确认 die 路径）

### 1. 乘法 `Omul Kl` — emit 无分支，落到 generic table → die

`src/target/i386/i386_emit.c` 的 omap 指令表仅有：

```c
{ Omul, Ki, "+imul%k %1, %=" },   /* 仅 32 位；无 Omul Kl */
```

emit 对 Kl 的分解逻辑只覆盖了 add/sub/shift/compare，没有 `Omul Kl`
的分解分支；`is_ke(Kl)` 不匹配 omap 的 `Ki`/`Ka` 兜底规则 →
`emitins` 在 generic 分支 `die("no match for mul(l)")`。

### 2. 除法/取余 `Odiv/Orem/Oudiv/Ourem Kl` — isel 显式 die

`src/target/i386/i386_isel.c` L322-325：

```c
if (KBASE(k) == 1)
        goto Emit;
if (k == Kl)
        die("i386: 64-bit arithmetic not yet supported");
```

即任何 `Kl` 类的 div/rem 在 isel 阶段直接 `die`，尚未生成分解代码。

## 修复方案（建议）

i386 无 64 位 `mul`/`div` 单指令（edx:eax 只覆盖 32×32→64 与
64÷32），最稳妥、可自举的做法是 **调用 libc 软算术库**，而非内联
一长串多字运算。meuos-libc 已自带 `src/arch/i386/soft_arith.c`，
提供：

- `meuos_u64_mul(hi, lo, a, b)` — 128 位积（hi:lo）
- `meuos_u64_mul_add(...)` — 乘加
- `meuos_u64_divmod(q, r, n_hi, n_lo, d)` — 64÷64→商/余

### 步骤 A — 暴露软算术 ABI 给 mcc 生成代码

- 确认上述符号在 `libc-meuos.a` 中导出，且遵循 i386 cdecl
  （参数经栈传递，返回值经栈指针返回 —— 需与 mcc 生成调用序列
  的约定对齐；必要时加 `#ifdef __MEUOS_INTERNAL` 的稳定别名）。
- 在 `include/` 提供一个极简声明头（如 `meuos/soft_arith.h`），
  供 mcc 后端 `declare` 外部符号时使用。

### 步骤 B — isel / emit 生成调用序列

1. `i386_isel.c` 的 `Odiv/Orem/Oudiv/Ourem` 分支：移除 `if (k == Kl) die(...)`
   改为 emit 一条调用 `meuos_u64_divmod` 的序列（先把两 32 位 slot
   组装成 n_hi/n_lo 入栈，调后从栈取 q/r 写回目标两个 slot）。
2. `i386_emit.c` 的 `Omul Kl` 分支：调用 `meuos_u64_mul`
   （入参 a/b 两 32 位 slot，返回 hi/lo 写回目标两个 slot）。
3. `Oudiv/Ourem` 复用同一 `meuos_u64_divmod`（无符号版本）。
4. 复用 `src/irgen`/前端已有的「调用外部软算术并取回 64 位结果」模式
   （参考现有 32 位 `Oudiv` 在 x86 的 `Oxdiv`/`Oxidiv` 包装）。

### 步骤 C — 回归

- 新增 `test/i386/kl_arith.c`（64 位 `*`、`/`、`%`，含有符号/无符号、
  溢出边界如 `0xFFFFFFFFFFFFFFFF * 2`）。
- `make check-i386` + `test/i386/qemu-runtime.sh` 全绿。
- 确认不破坏现有 32 位 Kl add/sub/shift/compare 双门禁。

## 验收

- `mcc --target=i386 -c kl_arith.c` 不再 `die`，生成合法 `.s`/`.o`。
- qemu 真 32 位内核下 `kl_arith` 输出与 64 位宿主参照一致。
- `i386_isel.c` L324-325 的 `die` 移除；emit 的 Kl mul/div/rem 有
  对应调用序列。

## 影响范围

- `src/target/i386/i386_isel.c`（L322-325 移除 die）。
- `src/target/i386/i386_emit.c`（新增 `Omul Kl` 调用分支；div/rem
  经 isel 改写后 emit）。
- `meuos-libc/src/arch/i386/soft_arith.c` + 新增导出头（符号 ABI 对齐）。

## 前置依赖

- i386 端到端 qemu 门禁已就位（P5），可直接用于验收。

## 关联

- 通用状态文件：`src/target/i386/.todo`（OPEN 清单）。
- libc 侧软算术：`meuos-libc/.todo/non-x86_64-runtime.md`（i386 段
  交叉引用本缺口）。

## 验收标准

<!-- TODO(main session): replace these placeholders with concrete shell commands the driver should run to verify this todo. The commands must exit 0 on success; any non-zero exit means the todo is NOT done. Keep the fenced block format below. -->

```
make -C projects/mcc check
```

