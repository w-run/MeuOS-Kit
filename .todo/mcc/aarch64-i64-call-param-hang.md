# aarch64 i64 函数调用传参 死循环（预存在，#19）

**状态**：🔶 open（登记 2026-08-06；x86-i64param 新增 rr_i64param 矩阵程序时暴露，pre-existing，与 i386 修复无关）

## 症状
aarch64 上任何带 i64 实参的函数调用（如 `long long addll(long long a,long long b){return a+b;} main(){addll(20,22);}`）→ qemu 运行时死循环（exit=124 timeout）。一元 i64 负/大常量截断也有类似挂。`(int)i64` 截断、i64 本地比较正常。

## 影响
新增矩阵程序 `rr_i64param` 在 aarch64 挂，已加 `progs_xfail="rr_i64param:aarch64"` 暂挂。其余 5 架构（i386/x86_64/arm/riscv64/loongarch64）PASS。

## 定位方向（未做）
aarch64 backend（aarch64_mabi.c selcall/selpar 的 i64 参数寄存器对约定 或 aarch64_memit.c）可能是寄存器对 ABI 或调用序列导致死循环。exclude me from scope; 排后续专项（任务 #19）。

## 关联
- x86-i64param（i386）主修复完成：`948fe982`（param 槽位）+ `f8a41276`（i64 槽位 base 一致性，#16）。
- loongarch64 `li.d` 大立即数（#20）也被同测程序暴露（`rr_i64param` 简化版小常量后不再触发）。
