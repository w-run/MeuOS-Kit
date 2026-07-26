# QEMU user-mode binaries（跨架构 qemu 运行时 gate）

> 这些二进制 **不进 git**——见本目录 `.gitignore`。AGENTS.md §4 禁止把
> 预编译二进制提交到仓库（宿主 bootstrapper 除外）；改从 QEMU 源码树自建，
> 各架构 bootstrap 脚本通过 `MEUOS_{ARCH}_QEMU=<path>` 环境变量注入。

## 获取方式

从 QEMU 10.1.0 源码树自建（已在 `env/build/qemu-10.1.0/` 中）:

```sh
cd env/build/qemu-10.1.0
./configure --target-list=aarch64-linux-user,riscv64-linux-user,loongarch64-linux-user,i386-linux-user \
  --disable-docs --disable-tools --disable-guest-agent \
  --disable-vnc --disable-gtk --disable-sdl --disable-cocoa \
  --disable-spice --disable-opengl --disable-virglrenderer \
  --enable-tcg --disable-werror \
  --prefix=$PWD/../qemu-install --ninja=/usr/bin/ninja
make -j$(nproc)
make install
cp ../qemu-install/bin/qemu-<arch> env/qemu/qemu-<arch>-static
```

## 当前状态

| 架构 | qemu-user | 来源 | 验证 | 测试脚本 |
|------|-----------|------|------|----------|
| aarch64 | ✅ 10.1.0 (自建) | `env/build/qemu-10.1.0` | 6/6 测试通过 | `test/aarch64-bootstrap.sh` |
| riscv64 | ✅ 10.1.0 (自建) | 同上 | 3/3 非线程通过 | `test/riscv64-bootstrap.sh` |
| loongarch64 | ✅ 10.1.0 (自建) | 同上 | 5/6 通过（TLS 受 mt/ld bug 限制） | `test/loongarch64-bootstrap.sh` |
| i386 | ✅ 10.1.0 (自建) | 同上 | 待验证 | `test/i386-bootstrap.sh` |
| x86_64 | 无需（宿主） | — | — | — |

## 运行 gate

```sh
# aarch64
MEUOS_AARCH64_RUN=1 MEUOS_AARCH64_QEMU=env/qemu/qemu-aarch64-static \
  ASCC=aarch64-linux-gnu-gcc sh test/aarch64-bootstrap.sh

# riscv64
MEUOS_RISCV64_RUN=1 MEUOS_RISCV64_QEMU=env/qemu/qemu-riscv64-static \
  ASCC=riscv64-linux-gnu-gcc sh test/riscv64-bootstrap.sh

# loongarch64
MEUOS_LOONGARCH64_RUN=1 MEUOS_LOONGARCH64_QEMU=env/qemu/qemu-loongarch64-static \
  ASCC=loongarch64-linux-gnu-gcc sh test/loongarch64-bootstrap.sh
```

## 验证基线（QEMU 10.1.0 / Linux x86_64）

| target | hello | atomic | setjmp | phase2 | bare-tls |
|--------|-------|--------|--------|--------|----------|
| aarch64 | "aarch64 MeuOS libc" | exit 0 | "setjmp ok" | "counter=2000" | "tls main=5 child=9..." |
| riscv64 | "riscv64 MeuOS libc" | exit 0 | "setjmp ok" | ⚠️ qemu-user 线程限制 | ⚠️ 同上 |
| loongarch64 | "loongarch64 MeuOS libc" | exit 0 | "setjmp ok" | "counter=2000" | ⚠️ mt/ld TLS LE reloc bug |
