# aarch64 qemu 运行 hello segfault

> 状态：🔄 开放（2026-08-04 由 exec-integration-lite 在聚合全量回归 2d4b65a 中发现）
> 关联 commit：无（非聚合引入，聚合前基线 df962a0 亦复现）

## 现象

- 聚合后 mcc 编译 `hello.c` + crt1 + libc，`qemu-aarch64-static` 运行 **segfault(139)**；
- 聚合**前**基线 df962a0 生成的 aarch64 汇编**逐字节一致**，同样 segfault(139)——故非聚合引入。

## 特征

- **带帧指针的 main**（`sub sp,sp` / `stp x29,x30` / `-ldr x30,x29` 序列）经 `mt/ld` + crt1 后崩溃；
- **手写无帧 main / 裸 _start / 汇编 main+ret** 均 exit=42，正常——指向栈帧序或 crt1/AAPCS 交互。

## 根因待查

- mcc aarch64 后端**栈帧序列**（prologue/epilogue 保存/恢复 x29/x30 的正确性）；
- 或 crt1 启动序与 AAPCS 调用约定交互。

## 范围

- mcc aarch64 后端：`src/target/aarch64/`；
- 可能涉及 crt1。

## 验收

- aarch64 qemu 运行 hello **返回 42**（不再 segfault）；
- `mcc → mt/as → mt/ld(+crt1+libc) → qemu-aarch64-static` 端到端 PASS；
- 不引入其它架构/既有门禁回归。

## 范围约束

- 先由 exec-mcc-lite 立项排查（`src/target/aarch64/` + 可能 crt1）；
- doc-pm 只登记与追踪；
- 修复后经验沉淀到 `.agents/knowledge/`。
