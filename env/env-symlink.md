# env/ 软链接

本 worktree 的 `env/` 已软链接到 main 分支的 env/：

```
env -> /workspace/MeuOS-Kit/env
```

**原因**: worktree 在创建时未继承 QEMU 构建产物和静态二进制。
主仓库的 env/ 包含已编译的 qemu-10.1.0 全架构静态二进制。

**包含的 qemu-user 静态二进制**: aarch64, i386, riscv64, loongarch64

**不包含**: arm (qemu-arm), x86_64 (使用宿主系统 qemu)

**qvm 管理器**: 从 worktree 的 `env/bin/qvm` 运行（与 main 共享相同脚本）。

**riscv64 thread_cpu 问题**: qemu 10.1.0 已修复此 bug，无需补丁。
