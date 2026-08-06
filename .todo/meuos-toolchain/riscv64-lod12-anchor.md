# mt/ld riscv64 PCREL_LO12 p_hi 锚点基准 bug（+0x14 应为 −0x14）

> 状态：✅ 已修复（2026-08-06, d45c813c）
> 关联：矩阵 `rr_global` riscv64 返回 42（正确）

## 修复

提交 d45c813c `mt/ld riscv64: fix PCREL/LO12 resolution (type 27/28 + both HI20 partners)`：

1. **reloc.c LO12 分支接受 type 27/28**：mt/as 和 GNU as 均发射 type 27/28（非 24/25），之前只判 24/25 导致配对块是死代码。
2. **HI20 配对扩展为两种名约**：
   - 约定 (a)：LO12 symbol = 局部标签 `.LrvpcN` → 按 `roff == p_off` 配对
   - 约定 (b)：LO12 symbol = 真实目标符号 → 按 `rsym == symbol_index` 配对
3. **支持绝对 HI20 伙伴**（R_RISCV_HI20 type 26，非 PC-relative 场景）
4. `lo12 = (S_hi + A_hi - P_hi) & 0xFFF`（PCREL）或 `(S_hi + A_hi) & 0xFFF`（绝对）

## 验证

- `make -C projects/meuos-toolchain check` PASS（无回归）
- 三处 `%pcrel_lo(.LrvpcN)` 的 `addi` 立即数均为负数（-20/-48/-64）
- `t0 = auipc_addr + 0x5000 - 0x14 = 0x406000`（`g` 的地址），数学正确
- `rr_global:riscv64` 从 xfail 移除

## 未覆盖

- `check-qemu-rtmatrix-riscv64` 需要 qemu-riscv64-static + sysroot，当前因 mcc 构建失败（C++模块链接问题）被阻塞 — 独立于 mt/ld 修复本身。