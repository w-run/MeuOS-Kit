# aarch64 qemu 运行 hello segfault

> 状态：✅ 已闭环（2026-08-04 exec-mcc-gp 推送 commit 40fec4a）
> 关联 commit：`40fec4a`（`tmp/exec-mcc/aarch64-segfault`）+ `2475c8d`（前置：缺入口块跳转）+ `ae60a9a`（前置：负偏移 ldur/stur）
> 经验沉淀：`.agents/knowledge/feedback_aarch64_aapcs.md`

## 现象（已闭环）

- 聚合后 mcc 编译 `hello.c` + crt1 + libc，`qemu-aarch64-static` 运行 **segfault(139)**；
- 聚合**前**基线 df962a0 生成的 aarch64 汇编**逐字节一致**，同样 segfault(139)——非聚合引入。

## 根因（实际）

聚合基线下 aarch64 后端有 **3 处独立缺陷**（exec-mcc-lite 此前已识别但未提交），叠加导致 `segfault(139)`/参数 spill 跨块读错：

1. **缺入口块跳转**（2475c8d，exec-mcc-gp 前置已推送）：prologue 后未 `b .L<name>.bb<start>`，带参函数 bb0 selpar 永远不执行 → 主路径 segfault(139)。
2. **selpar stack-arg 偏移错**（40fec4a）：`mabi_selpar` 用 `off=16`，注释把 `fp` 误当作 `sp-16`。AAPCS64 规定 `x29 = sp at function entry = caller_sp_at_call`，caller 把 stack-passed args 写到 `[sp_at_call + 0..]`，callee 读 `[x29 + 0..]`；原代码读到 caller 第 3 个 stack-arg 位置（偏移 16 字节）。
3. **大帧 stp 偏移超限**（40fec4a，`aarch64_memit.c`）：`stp x29,x30,[sp,#framesize-16]` 在 `framesize - 16 > 504` 时违反 signed imm7 限制；改成 `mov x16,x29; add x29,sp,#N; stp x16,x30,[x29,#-16]`，尾部 `[x29,-8]/[x29,-16]` 仍正确还原。
4. **spill-slot 基址寻址丢 x29**（40fec4a，`emit_addr_to_scratch`）：base 为 spilled temp 时原代码 `load_imm` 直接用 slot 偏移作地址，丢掉了"slot 里存的是指针"这一语义；改为 `ldr rn,[x29,#boff]`（与 loongarch64 对齐）。

## 验收（已通过）

- aarch64 qemu 运行 `hello.c` **返回 42**；
- aarch64 qemu 运行 `hello_args.c`（12 int 参数 > 8 reg args，触发栈 spill）：参数传递正确（`x = 1+2+...+12 = 78`），原用例 `return r - 84 + 42` 因常量笔误得 exit=36（修复前 exit=97）；改 `r - 78 + 42` 后 exit=42；
- `make -C projects/mcc check` 通过（smoke hello exit=0）；
- `sh projects/mcc/test/verify-all.sh` **PASS=19 FAIL=0 SKIP=0**（含 check-mir / check-pic-verify / check-c11 / check-c23 / check-abi / check-driver / check-mt-integration / 交叉目标 & runtime）；
- 不引入 x86_64/riscv64/loongarch64/i386 回归。

## worktree

保留：`/workspace/MeuOS-Kit/.agents/worktrees/exec-mcc/aarch64-segfault`（分支 `tmp/exec-mcc/aarch64-segfault`，HEAD=40fec4a）。
