# pid_t=long 与 gcc 内建 `int fork(void)` 原型冲突

> 状态：🔄 待专项
> 发现：2026-08-05（libc-worker，gcc 冒烟验收 + P2 批次）
> 严重度：低（gcc 编译 `-Wbuiltin-declaration-mismatch`，会触发 `-Werror` 拒）

## 现象
gcc 编译任何 include `<unistd.h>` 的 MeOUOS 程序时报警告：
`include/unistd.h:18:7: warning: conflicting types for built-in function 'fork'; expected 'int(void)'`。
若带 `-Werror` 直接编译失败。

## 根因
`include/sys/types.h:18` 定义 `typedef long pid_t;`，而 Linux `fork(2)` 实际返回
32 位 `int`，gcc 内建表里 `fork` 原型是 `int fork(void)`。故 `pid_t fork(void)` =
`long fork(void)` ≠ `int fork(void)`，类型冲突。

> 注：POSIX 下 Linux 的 `pid_t` 应是 `int`（非 `long`）。`long` 是错误的。

## 修复方向（二选一，均属语义变更，需评估 /etc 与 ABI 布局影响）
1. **改 `pid_t` 为 `int`**（正确 POSIX 类型）：影响 `sys/types.h`、`sched.h`、
   `signal.h`（siginfo/sigaction 内嵌 pid_t）、`spawn.h` 等。因 Linux ABI 本就
   int，改后结构布局更正确；需全量回归 + gcc 冒烟确认无新冲突。
2. **不改类型**：给 `unistd.h` 的 `fork` 声明加 gcc 内建抑制
   （如 `__attribute__((__nothrow__))?` 或 `#pragma GCC diagnostic`），仅消警告
   治标，不治类型根因。

## 约束
- 本批次（P1/P2）聚焦符号补齐，未顺手改 `pid_t`（避免 `sys/types.h` 布局侧
  效应波及过大，符合「别顺手做大重构」边界）。单独成专项做 + 全量回归。

## 验证
修复后：gcc 编译 `#include <unistd.h>` 无 builtin-mismatch 告警；libc make check
全绿；gcc 端到端冒烟 exit 0。
