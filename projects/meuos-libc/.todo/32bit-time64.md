# 待实现：32 位目标统一使用 64 位 time_t

## 背景

i386 当前的公共头仍把 `time_t` 定义为 `long`，在 32 位目标上会回到 32 位秒数。
这与 meuos-libc 的架构策略冲突：i386 和后续 armhf 必须使用 64 位 `time_t`，从
ABI 层根除 2038 问题，而不是依赖宿主 libc 的 `_TIME_BITS` 宏。

## 范围

- 将 i386（以及新增 armhf）公共 `time_t` 固定为有符号 64 位；
- 梳理 `off_t`、`suseconds_t`、`timespec/timeval`、`stat` 等相关布局；
- 为 `clock_gettime`、`nanosleep`、`time`、`futex` 等时间参数选择 time64 syscall，
  并正确处理 32 位寄存器的 64 位参数拆分和对齐槽位；
- 增加 `INT32_MAX` 附近、2038-01-19 之后和负时间值测试；
- 在 i386 QEMU 门禁中验证静态程序，armhf 加入后复用同一测试矩阵。

## 验收

1. `sizeof(time_t) == 8` 在 i386 和 armhf target 成立；
2. 不依赖宿主 libc，目标静态程序能读写 64 位时间值；
3. `make ARCH=i386 check-i386-bootstrap` 与对应 QEMU time64 回归通过；
4. 32 位 syscall wrapper 不再把公共 API 的时间参数截断为 `long`。
