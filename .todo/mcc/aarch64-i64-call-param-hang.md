# aarch64 i64 函数调用传参 死循环（预存在，#19）

**状态**：✅ closed（2026-08-06，`fda34544`）

## 症状
aarch64 上任何带 i64 实参的函数调用（如 `long long addll(long long a,long long b){return a+b;} main(){addll(20,22);}`）→ qemu 运行时死循环（exit=124 timeout）。一元 i64 负/大常量截断也有类似挂。`(int)i64` 截断、i64 本地比较正常。

## 影响
新增矩阵程序 `rr_i64param` 在 aarch64 挂，已加 `progs_xfail="rr_i64param:aarch64"` 暂挂。其余 5 架构（i386/x86_64/arm/riscv64/loongarch64）PASS。

## 根因（已修 `fda34544`）
**不是** i64 参数寄存器对 ABI 错配，而是 aarch64_memit 的 **JCC 终止子缺少 s2 fallthrough 显式跳转**：
- emit_block 的 MMOP_JCC 只发一条条件分支（cbnz/cbz/b.cc）到 s1（taken），s2（fallthrough）靠**物理块相邻**。
- 但块按 `fm->link` 顺序发射，不保证 s2 相邻。多条件检查的 `main`（矩阵 rr_i64param 有 3 个顺序 if）中，某 JCC 条件为假时 fallthrough 落到任意块（如 entry 派发块 `.bb0: b .bb1`），**无限循环 → exit=124**。
- 修法：对齐 x86_64（emit_block JCC 后无条件 `b .L<fn>.bb<s2>`），每条 JCC 显式补 s2 跳转。新增 `test/aarch64/jccfall.c`（多顺序 if）+ runtime.sh 编译 gate（断言每个 cbnz/cbz 后紧跟 `b`）。`rt_matrix.sh` 解除 `rr_i64param:aarch64` xfail，6 架构 9 程序全 PASS。

## 关联
- x86-i64param（i386）主修复完成：`948fe982`（param 槽位）+ `f8a41276`（i64 槽位 base 一致性，#16）。
- loongarch64 `li.d` 大立即数（#20）也被同测程序暴露（`rr_i64param` 简化版小常量后不再触发）。
