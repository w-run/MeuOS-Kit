<!--
priority: P2
status: pending
kind: impl
note: 切到 ${MEUOS_SYSROOT}/bin/sh;前置是 MeuOS userspace 自身的 shell 落地
-->

# 待实现：MeuOS 原生 shell 替代 /bin/sh

## 背景
配方命令当前通过宿主 `/bin/sh -c` 执行（`src/exec.c` 的 `run()`）。
在完整 MeuOS userspace 出现前，这是有意的过渡依赖。

## 目标
当 MeuOS userspace 提供自身 shell 后，将 `execve("/bin/sh", ...)` 切换为
MeuOS sysroot 内的 shell（例如 `${MEUOS_SYSROOT}/bin/sh`）。

## 影响范围
- `src/exec.c` 的 `run()`：shell 路径解析。
- `check-sysroot-static` 的注释需同步更新。

## 验收
- 在 chroot 到 `${MEUOS_SYSROOT}` 时，`meow build` 仍能执行配方命令。

## 验收标准

<!-- TODO(main session): fill in concrete commands. -->

```
make -C projects/meow check
```

