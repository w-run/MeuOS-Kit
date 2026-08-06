---
name: x86_64 静态全局数组 segfault 闭环
description: x86_64 静态 exe 全局数组 segfault 的根因是 mt/as 中 imul $imm, %reg 2-操作数编码缺失
type: project
---

# x86_64 静态全局数组 segfault 闭环

**状态**: ✅ 已闭环 (2026-08-07)

## 根因

x86_64 静态可执行文件中的全局数组访问在运行时报 segfault(139)。崩溃点在 crt 初始化阶段或 main 内数组访问。

**根本原因**: `mt/as` (x86_64 编码器) 中，`imulq $4,%rax` 这类 2-操作数立即数乘法（mcc 为数组索引缩放而发射）缺少正确处理分支：

- `mt/as encode.c:x86_64_encode_insn` 中 `imul` 只有两个分支：
  - `n == 3 && op[0] imm`: 3-操作数 `imul $imm, rm, reg` 形式 (Op 69) — 正确
  - `n == 2 && op[1] reg`: 回退到 2-操作数 `imul r64, r/m64` (0x0f 0xaf) 分支，该分支期望 op[0] 是 reg/mem 操作数

- op[0] 是立即数时，modrm 把 imm32 编码为 r/m 操作数，并拼上了流浪的 PC32 重定位的 imm32 addend，导致后续每条指令偏移几个字节，产生垃圾代码（`imul -0x77(%rax), %rcx`）。

## 修复

**`dfcc0dc7`** — `mt/as x86_64 encode.c`: 在 `n == 2 && op[0].kind == OP_IMM && op[1].kind == OP_REG` 时，编码为 `imul %reg, %reg, $imm32` (Op 69, REX.W + 69 + modrm(reg=op[1], rm=op[1]) + imm32)。

**`9e65b72d`** — mcc: 添加回归测试 `test/c99/static_global_array.c`（覆盖 .data 读/写、.bss 索引缩放、全局指针、指针运算）。

**`9f1cf2be`** — test: 添加 x86_64 runtime 回归批次 `check-x86_64-runtime` 到 verify-all 门禁。

## 验证

- 宿主 `gcc` 路径（mcc 默认调用宿主 `cc -x assembler`）不受此影响——宿主 as (GNU as) 正确编码 `imul $4,%rax`
- `MT_AS`/`MT_LD` 路径（mt/as + mt/ld）受影响
- 通过 `qemu-x86_64-static` 验证：静态 exe + 全局数组运行正常，exit=0
- 无 TLS 场景同样通过
- 不引入其它门禁/架构回归