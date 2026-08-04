---
name: aarch64 AAPCS64 stack-arg / spill / frame 三处缺陷闭环
description: aarch64 后端 #43 闭环经验：selpar off=0、大帧 stp 超限、spill-slot 基址寻址丢 x29
type: feedback
---

# aarch64 AAPCS64 三处缺陷闭环（2026-08-04）

## Why

`qemu-aarch64-static` 跑带参函数（>8 reg args）参数 spill 后读到 caller 第 3 个 stack-arg 位置（偏移 16 字节），错误根因源于 `mabi_selpar` 一行注释把 `fp` 误当作 `sp-16`。同一文件还隐藏着另外两个独立缺陷（大帧 stp 超限、spill-slot 基址寻址丢 x29），叠加导致 segfault(139)。

## 根因与修复（commit 40fec4a，squashed 3 修）

### 1. selpar off=16 → 0（`aarch64_mabi.c`）

```c
// 错：
int off = 16;   /* caller-pushed stack args sit at fp+16
                * (the prologue pushed fp+lr, so fp = old_sp - 16) */
// 对：
int off = 0;    /* AAPCS64 §Frame Pointer: x29 = sp at function entry
                * = sp_at_call, args land at [x29 + 0] */
```

**AAPCS64 关键事实**：x29 (fp) 在 callee prologue `sub sp,sp,#N; stp x29,x30,[sp,#N-16]; add x29,sp,#N` 之后指向 **caller 的 sp_at_call**（即 callee 入栈前的 sp），不是 sp_at_call-16。caller 把 stack-passed args 写到 `[caller_sp_at_call + 0..]`，callee 读 `[x29 + 0..]`。注释把 fp/lr save area（[x29, -16] 与 [x29, -8]）误放在 args 上方 16 字节处——这些位置在 saved x29/x30 之**下**（即 sp 减小方向），不是 args 上方。

### 2. 大帧 stp 偏移超限（`aarch64_memit.c mfnm_emit_aarch64`）

`stp x29,x30,[sp,#N-16]` 的 signed imm7 限制 `[-512, 504]`（按 8 缩放）。当 `framesize - 16 > 504`（即 framesize > 520），原汇编非法。修复：先 `mov x16, x29` 把旧 fp 暂存到 scratch，加 x29 后再 `stp x16, x30, [x29, #-16]`。尾部 `[x29, #-8]` 读 x30、`[x29, #-16]` 读 x29 不变（x29 现在指向 fp/lr save 区）。

### 3. spill-slot 基址寻址丢 x29（`aarch64_memit.c emit_addr_to_scratch`）

base 为 spilled temp（`kind == MV_TEMP && reg < 0`）时，slot 里存的是**指针值**（alloca 结果 / lea 计算出的指针），不是地址本身。原 `load_imm(rn, slot + g_slot_base)` 把 slot 偏移当地址用，丢了 load 步骤。修复：`ldr rn,[x29,#boff]`，再 `add rn,rn,#off`（与 loongarch64 `emit_addr_to_scratch` 对齐）。

## 顺带：缺入口块跳转（commit 2475c8d，exec-mcc-gp 前置推送）

prologue 后必须 `b .L<name>.bb<start>` 跳到 selpar bb0，否则 framed 函数读未初始化入参寄存器 → segfault。x86_64/riscv64 都有，aarch64 漏了。

## How to apply

- 任何 AAPCS64 参数/栈帧改动前，先 grep `int off =`、`x29` 看 caller/callee 双方对称；
- 写 prologue 后必须紧接 `if (fm->start) fprintf(... "b .L%s.bb%u\n", ..., fm->start->id)`；
- 大帧 spill `stp`/`ldp`/单 `ldr/str` 三类指令的 signed imm 限制：base+offset scale 后的范围是 `[-256, 255]` for 8-byte/单 reg（`ldr`/`str` w/x）、`[-512, 504]` for `stp`/`ldp` 16-byte 对（scaled by 8）；
- spill slot 寻址：`ldr rn,[x29,#slot+g_slot_base]` 然后 `add rn,rn,#off`，**绝不** `load_imm(rn, slot+off)` 直接当地址；
- 验证：12-int-arg 函数（>8 reg args）必须能在 qemu 端到端跑通才能算"PASS"。