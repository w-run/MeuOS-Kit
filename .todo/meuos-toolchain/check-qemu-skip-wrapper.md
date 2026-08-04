# check-qemu-x86_64 / check-qemu-i386 缺 SKIP 包装 + 路径写死

> 状态：🔄 开放（pre-existing，2026-08-04 由 reviewer-auditor 在 rtld P0 验收中发现）
> 分支参考：tmp/rtld-p0
> 关联文件：projects/meuos-toolchain/Makefile

## 现象

Makefile line 312-323（`check-qemu-x86_64` / `check-qemu-i386`）：
- 没有 aarch64/riscv64/loongarch64/arm 那种 `if [ "$$rc" = "0" ]; then PASS; else SKIP` 包装；
- 路径写死 `../../env/qemu/qemu-$$arch-static`，但 worktree 下 `../../env/qemu` 不存在（实际 qemu 二进制在项目根 `/workspace/MeuOS-Kit/env/qemu/`）。

## 影响

- 在 worktree 跑 `make check` 时若调用这两个目标，会 exit 错误；
- 但当前 `make check` 主列表（Makefile:168）不包含它们，故本次 rtld P0 验收未受影响；
- worktree 后续若把这两个加进 `make check` 就会爆炸。

## 修复方向

- 加 SKIP 包装（参照 check-qemu-aarch64 line 257-267）；
- 或修复 worktree 下的 env/qemu 路径解析（参考 `.agents/reference/build-reference.md` 与 env/env-symlink.md）；
- 最简：先加 SKIP 包装，与其它架构一致。

## 验收

- `make -C projects/meuos-toolchain check-qemu-x86_64` 在 worktree 缺 qemu 时输出 `SKIP (no QEMU)` exit 0；
- `make -C projects/meuos-toolchain check-qemu-i386` 同上；
- `make -C projects/meuos-toolchain check` 不引入回归。

## 范围约束

- 仅 Makefile；
- 不动 src/*；
- 文件级 git commit `mt: add SKIP wrapper for check-qemu-x86_64 and check-qemu-i386`。
