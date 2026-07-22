# QEMU user-mode static binaries (可选，用于 aarch64 bootstrap qemu 运行时 gate)

> 这些二进制 **不进 git**——见本目录 `.gitignore`。AGENTS.md §4 禁止把
> 预编译二进制提交到仓库（宿主 bootstrapper 除外）；按需从上游 release 下载，
> 跑 `make check-aarch64-bootstrap` 时通过 `MEUOS_AARCH64_QEMU=<path>` 注入。

## 获取方式

```sh
# multiarch/qemu-user-static 是 QEMU 官方维护的静态 qemu-user 发布。
# v7.2.0-1 是当前仓库验证过的版本。
curl -L -o /usr/local/bin/qemu-aarch64-static \
  https://github.com/multiarch/qemu-user-static/releases/download/v7.2.0-1/qemu-aarch64-static
chmod +x /usr/local/bin/qemu-aarch64-static
```

或放进工作树本地（不提交）：
```sh
mkdir -p env/qemu
curl -L -o env/qemu/qemu-aarch64-static \
  https://github.com/multiarch/qemu-user-static/releases/download/v7.2.0-1/qemu-aarch64-static
chmod +x env/qemu/qemu-aarch64-static
```

## 运行 gate

```sh
make -C projects/meuos-libc check-aarch64-bootstrap
# 默认只交叉编译并验证 ELF64/AArch64 头。
MEUOS_AARCH64_RUN=1 MEUOS_AARCH64_QEMU=/usr/local/bin/qemu-aarch64-static \
  make -C projects/meuos-libc check-aarch64-bootstrap
# 期望输出: aarch64 MeuOS libc / counter = 2000 / tls main=5 child=9 errno=31/47
```

## 验证基线（v7.2.0-1 / Linux x86_64）

| target | hello | atomic | phase2 | bare-tls |
|---|---|---|---|---|
| aarch64 (qemu-user 7.2.0) | "aarch64 MeuOS libc" | exit 0 | "counter = 2000" | "tls main=5 child=9 errno=31/47" |
