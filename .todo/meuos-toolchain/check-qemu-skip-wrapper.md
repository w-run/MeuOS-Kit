# check-qemu-x86_64 / check-qemu-i386 缺 SKIP 包装 + 路径写死

> 状态：✅ 无需改动（基线 df962a0 已含 commit de49e414 修复）
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

## 核实记录（2026-08-04 exec-toolchain-lite-2）

- **commit**：`de49e414`（"ci: 添加 check-qemu-x86_64/i386 + 6 架构统一 QEMU 测试"，2026-07-31，已合入基线 df962a0）。
- **Makefile 312-324 现状摘要**：
  - 路径由 `../../env/qemu/...` **写死改为 abspath 动态解析**（经 `$(abspath ...)` 定位项目根 `env/qemu`，规避 worktree 下相对路径失效）；
  - 缺 QEMU 二进制 → `SKIP (no QEMU)`；
  - 缺 sysroot → `SKIP (no sysroot)`；
  - 与 aarch64/riscv64/loongarch64/arm 的统一 SKIP 包装一致。
- **实测命令与结果**：
  - `make -C projects/meuos-toolchain check-qemu-x86_64` / `check-qemu-i386`：worktree 无 QEMU 时输出 `SKIP (no QEMU)`，exit 0；
  - `make -C projects/meuos-toolchain check`：全 PASS，无回归。
- **误报说明**：该缺陷已在 2026-07-31 的 de49e414 合入基线 df962a0 后不复现；reviewer-auditor 于 rtld P0 验收时按旧情 reported，属**信息滞后**导致，无需代码改动，本待办关闭为 no-op。
