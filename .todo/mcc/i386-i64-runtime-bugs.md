# i386 i64 预存在运行时 bug（x86-i64param 验证时新发现）

**状态**：登记 2026-08-06。战场：mcc i386 后端一致性战役（mcc-worker）。

## 1. i64 调用结果 == 常量 比较错槽位  → ✅ 已闭环
- 原症状：`emit_setccr` 把 i64 EQ 比较的两个半字从错槽位读取。
- 现状：实测 `f(42LL)==42LL` 在 i386 运行时 exit=0（与 x86_64 一致），且
  `regress.sh` 的 i64cmpeq gate（`sete %al` + `movzbl` 零扩展）已通过。
  判定为已修复（由来历不明的更早提交或 i64 槽位重构解决）。关闭。

## 2. i64→i32 截断经 shl/sar 32，且下游 i64== 比较不窄化  → 🔶 open（跨域，需 team-lead 拍板）
- 症状：`(int)r`（r 为 i64）被降级为 i64 `shl 32 ; sar 32` 序列；该序列本身
  发射正确（实测 asm 数学正确：{lo,hi}<<32>>32 得到 sign-extended lo）。但
  **下游 `==` 比较在 i386 上按 64 位比较**，而 x86_64 按 32 位比较。
- 实测：
  - `return (int)r;` 直接返回 → i386 eax 正确（0x9ABCDEF0）。
  - 但是 `(int)r == 0x9ABCDEF0` → i386 返回 1（错），x86_64 返回 0（对）。
    原因：i386 把比较做成 i64==，左操作数为 {0x9ABCDEF0, 0xFFFFFFFF}（hi=符号扩展），
    右常量为 {0x9ABCDEF0, 0}，hi 半字不一致 → 判不等。
- 根因：这是**共享 MIR 降级**问题，非纯 i386 emit 宽度 bug：(int) 应降级为干净的
  i32 trunc（各后端 emit 成 `movl lo, eax`），而非 i64 移位对；同时 i386 的 i64==
  比较未像 x86_64 那样在 int 语境下窄化到 32 位。
- 影响：任何 `(int)longval` 参与比较/条件分支时 i386 与 x86_64 行为不一致。
- 决策点（跨域）：要么把 (int) 降级改成干净 i32 trunc（影响所有后端，需 team-lead
  授权改共享 lowering），要么让 i386 的 i64== 比较在可证的 32 位符号扩展操作数上
  只比低 32 位（i386 局部、脆弱）。**mcc-worker 不自作主张改共享 lowering**，先登记并上报。
- 诊断法：`(int)longval` / `(int)longval == K` 看是否出现 `Li64sh` 序列及 i64== 比较。

## 3. mt/as 拒绝 `cdq`（须 `cltd`）  → ✅ 已闭环（战役 #18）
- 已全量 `cdq`→`cltd`（MOVSX/除法/i64_base 寄存器物化 fallback 等 site 均已切换）。
- 本项与 i386 emit 宽度一致性无关，关闭。

## 4. i64 除法/取余 (DIV/REM) 落到 32 位 idivl，i386 SIGFPE  → 🔶 open（跨域：需 libc 软除 helper）
- 症状：`long long a/b`、`a%b`（i64）编译为对低 32 位单独 `idivl`，高半字未参与 →
  除零或结果错；实测 `q(-100000000000LL,7LL)` 直接 SIGFPE（浮点异常/core dumped）。
- 根因：i386_memit.c 的 `MMOP_DIV/UDIV/REM/UREM` 在 `dtype==MT_I64` 分支里只处理了
  ADD/SUB/SHL/SHR/SAR/MUL（MUL 已本战役修），DIV/REM 落到 line ~1005 的 32 位
  `idivl`/`divl` 回退路径，只除低半字。与 MUL 同类（i64 操作漏接 i64 专门路径）。
- 正确实现二选一（需 team-lead 拍板，跨 libc 组件）：
  (a) 内联 64/64 算法（shift-subtract，~50 行 asm，自包含，不依赖 libc）；
  (b) 降级为调用 `__divdi3`/`__udivdi3`/`__moddi3`/`__umoddi3` 软除 helper（与 GCC 一致）。
- 关键阻塞：meuos-libc 的 `libgcc_meuos/div.c` 提供这些符号，但 **i386 sysroot 的
  `libc-meuos.a` 未含它们**（`nm` 查无）。即便 mcc 发射调用，i386 libc 也无法解析。
  → 需与 libc-worker 协同：i386 libc 构建纳入 libgcc 软除，且 mcc 决定内联还是调 helper。
- 影响：任何 i64 除法/取余在 i386 上崩溃或错，与 x86_64（`idivq`）行为不一致。
- 诊断法：`long long a/b` 看是否出现 `idivl`（32 位）而非 `mull` 类分解或 `__divdi3` 调用。

## 关联修复（本战役已提交）
- `d3926c80`：emit_load i64 高半字地址 `movl 4+,` 畸形 → MAddr copy+off+=4 重构。
- `182a7b0c`：i64 NEG/NOT 半字存储裸寄存器名 `movl edx` → 加 `%`（与 #22a/#22b 同类）。
- `3477e6a1`：MOVSX 源宽度取法对齐 x86_64（`s0->type`），修符号扩展丢失（#22b）。
