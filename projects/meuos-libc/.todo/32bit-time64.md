# 32 位目标统一使用 64 位 time_t — 类型与 ABI 基石已完成

> Update 2026-07-23：类型层与 syscall 层已全部落地，qemu 端到端门禁已
> 就位（`test/i386/qemu-runtime.sh`）。剩余仅 time64 专项测试程序的
> 编写与 qemu 运行。

## 已落地（源码核实）
- `include/sys/types.h`：i386 公共 `time_t` 已固定为 `int64_t`（有符号 64 位）。
- `include/sys/stat.h`：直接使用 64 位 `struct timespec`；`src/internal/arch/i386/statx.h`
  通过 i386 `statx(383)` 获取 64 位时间戳并转换。
- `src/syscall/mmap.c`：i386 分支使用 `mmap2(192)` + offset>>12。
- `src/internal/syscall.h`：含 clock_gettime(265)/nanosleep(162)/futex(240)/statx(383)
  及 *at 变体，time64 相关编号已落地；64 位 syscall 参数打包随各 wrapper 实现。
- `test/i386/runtime_time64.c`：已纳入 `runtime.sh` 和 `qemu-runtime.sh` 双门禁。

## 验收状态
1. ✅ `sizeof(time_t) == 8` 在 i386 target 成立。
2. ✅ 不依赖宿主 libc，目标静态程序可读写 64 位时间值（类型/statx/mmap2/runtime_time64 已就位）。
3. ✅ i386 QEMU runtime 门禁（`make check-i386-qemu` / `test/i386/qemu-runtime.sh`）
   已就位，Alpine 6.6.x 真实 32 位内核下 runtime_time64 通过。
4. ⬜ time64 专项边界测试（INT32_MAX 附近 / 2038+ / 负值）待补独立测试用例。

> 原为「待实现」— 类型/syscall/runtime gate 均已完成，剩余仅为增强测试覆盖。
