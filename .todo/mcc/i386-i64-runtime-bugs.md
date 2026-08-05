# i386 i64 预存在运行时 bug（x86-i64param 验证时新发现，登记后续专项）

**状态**：🔶 open（登记 2026-08-06；x86-i64param 排查过程中发现，均 pre-existing，base 同样存在，非本次引入）

## 1. i64 调用结果 == 常量 比较错槽位
- 症状：`long long f(c){return c;} main(){return f(42LL)==42LL?42:1;}` 里 emit_setccr 把 f 结果(`[ebp-16]` lo / `-12` hi) 与常量(`-32` lo / `-28` hi) 交错比较：`movl -16,%eax; movl -28,%ecx; cmpl`（应 -16 vs -32 和 -12 vs -28）。base `retconst` 同样有此错。影响任何 i64 函数返回值比较。
- 位置：`i386_memit.c::emit_setccr`（i64 分支用 `i64_base(a,0)/i64_base(b,1)`，但调用结果 value 的 slot 与存储位置不一致）。疑与 i64 value 的 slot 分配/存储连接有关。
- 诊断法：`mcc -target i386 -S` 看比较段 `cmpl` 的两个操作数是否分别来自同一条运算的 lo/hi。

## 2. i64→i32 截断经 shl/sar 32
- 症状：`(int)r`（r 为 i64）被降级成两次 i64 移位（shl 32 + sar 32）再取低半，令人费解且该路径有 bug（结果错）。
- 应为直接取低 32 位（`movl slot,%eax`）。属 32 位后端的 i64 窄化 cast 未做简单截断。
- 诊断法：`(int)longval` 看是否出现 `shll $32` / `sarl $31` 而非直接 movl 低半。

## 3. mt/as 拒绝 `cdq`（须 `cltd`）
- 症状：mcc 发射 `cdq` 被项目自带 mt/as 拒绝（`as: unsupported instruction: cdq`），但 `cltd` 通过（已最小化验证）。base 的 MOVSX/除法/i64_base 寄存器物化 fallback 均发 `cdq`。
- mt/as 源码 `target/i386/encode.c:1063` 有 `cdq`/`cltd` 双路径，但内置 as 未命中 `cdq` → 要么 mt/as 修，要么 mcc 全量 `cdq`→`cltd`。涉及多 site（i386_memit.c MOVSX/除法/i64_base、可能 i386_emit.c）。

## 关联
- x86-i64param 主体（i64 栈参/实参/返回槽位）已修（i386_mabi.c selpar/selcall/selret/vaarg + memit RET），本 3 项是独立的 i64 运行时正确性缺陷，修完可让 i386 i64 端到端（含 qemu 运行时）闭环。
