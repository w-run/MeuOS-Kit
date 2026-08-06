# PIE 动态链接里 libc .bss 全局缺 R_X86_64_RELATIVE（线程控制崩）

> 状态：✅ 已闭环（2026-08-07 toolchain-pie-worker）
> 闭环 commit：`ed78880f`（mcc-dev）
> 回归门：`ld_pie_e2e.sh`（check-ld-pie）

## 修复

`ed78880f` mt/ld PIE 模式为 .data/.bss 数据符号生成 R_X86_64_RELATIVE 重定位。

## 验证

- `ld_pie_e2e.sh`：.data + .bss 符号的 RELATIVE 条目生成 ✓
- 多架构 .bss 符号引用在 PIC 场景下通过 GOT + RELATIVE 正确修复
