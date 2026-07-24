# QEMU user-mode static binaries（跨架构 qemu 运行时 gate）

> 这些二进制 **不进 git**——见本目录 `.gitignore`。AGENTS.md §4 禁止把
> 预编译二进制提交到仓库（宿主 bootstrapper 除外）；按需从上游 release 下载，
> 各架构 bootstrap 脚本通过 `MEUOS_{ARCH}_QEMU=<path>` 环境变量注入。

## 获取方式

```sh
# multiarch/qemu-user-static 是 QEMU 官方维护的静态 qemu-user 发布。
# v7.2.0-1 是当前仓库验证过的版本。
curl -L -o /usr/local/bin/qemu-<arch>-static \
  https://github.com/multiarch/qemu-user-static/releases/download/v7.2.0-1/qemu-<arch>-static
chmod +x /usr/local/bin/qemu-<arch>-static
```

或放进工作树本地（不提交）：
```sh
mkdir -p env/qemu
curl -L -o env/qemu/qemu-<arch>-static \
  https://github.com/multiarch/qemu-user-static/releases/download/v7.2.0-1/qemu-<arch>-static
chmod +x env/qemu/qemu-<arch>-static
```

## 当前状态

| 架构 | qemu-user-static | 来源 | 验证 | 测试脚本 |
|------|-----------------|------|------|----------|
| aarch64 | ✅ v7.2.0 | multiarch/qemu-user-static | 6/6 测试通过 | `test/aarch64-bootstrap.sh` |
| riscv64 | ✅ v7.2.0 | 同上 | 3/3 非线程通过 | `test/riscv64-bootstrap.sh` |
| loongarch64 | ✅ v7.2.0 | 同上 | 待验证 | `test/loongarch64-bootstrap.sh` |
| x86_64 | 无需（宿主） | — | — | — |
| i386 | 无需（ia32 仿真） | — | — | — |

## 运行 gate

```sh
# aarch64
MEUOS_AARCH64_RUN=1 MEUOS_AARCH64_QEMU=env/qemu/qemu-aarch64-static \
  make -C projects/meuos-libc check-aarch64-bootstrap

# riscv64
MEUOS_RISCV64_RUN=1 MEUOS_RISCV64_QEMU=env/qemu/qemu-riscv64-static \
  ASCC=riscv64-linux-gnu-gcc sh test/riscv64-bootstrap.sh

# loongarch64
MEUOS_LOONGARCH64_RUN=1 MEUOS_LOONGARCH64_QEMU=env/qemu/qemu-loongarch64-static \
  ASCC=loongarch64-linux-gnu-gcc sh test/loongarch64-bootstrap.sh
```

## 验证基线（v7.2.0-1 / Linux x86_64）

| target | hello | atomic | setjmp | phase2 | bare-tls |
|--------|-------|--------|--------|--------|----------|
| aarch64 | "aarch64 MeuOS libc" | exit 0 | "setjmp ok" | "counter=2000" | "tls main=5 child=9..." |
| riscv64 | "riscv64 MeuOS libc" | exit 0 | "setjmp ok" | ⚠️ qemu-user 7.2 限制 | ⚠️ 同上 |
| loongarch64 | 待验证 | 待验证 | 待验证 | 待验证 | 待验证 |
