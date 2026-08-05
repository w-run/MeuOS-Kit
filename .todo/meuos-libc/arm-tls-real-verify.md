# arm TLS 真验待办（qemu-system / 真机路径）

> 状态：🔄 待专项（基建投入）
> 发现：2026-08-05（libc-worker，多架构真跑验证 + arm TLS 专项根因）

## 问题
arm 的 `_Thread_local`(error/errno、`_meuos_exc` 异常 state) 在 **qemu-arm-static (v10.1.0)** 下**无法真跑验证**：
- kuser set_tls（`ldr r1,=0xffff0fe0; bx r1`）：qemu 下静默 no-op，不真正设 TPIDRURO。
- 直接 `mcr p15,0,r0,c13,c0,3`（user-space 合法，bionic/musl arm 做法）：qemu 下 **SIGILL**。
- → `mrc` 读 TPIDRURO 回退后非期望值 → `_Thread_local` 访问落低/垃圾地址 → SIGSEGV。

**已判定为 qemu-user ARM TPIDRURO 仿真限制，非 libc 运行时 bug**（真机 ARM Linux kuser page 存在、TPIDRURO 可写，libc TLS 会正常）。

## 真验路径（三选一）
1. **qemu-system + ARM Linux 内核**（full-system，TPIDRURO 经内核 kuser 真实可写）——最贴近真机、推荐。
2. **真 ARM 硬件/树莓派类**。
3. 换一个完整仿真 ARM TPIDRURO 的 qemu-user 变体。

## 验收目标
qemu-system/真机上：arm `errno` 读写、`_meuos_exc_throw`（register→throw→longjmp→catch）不再 SIGSEGV，exit 0；arm-bootstrap 运行时 gate 可加 TLS 断言。

## 关联
- `test/arm-bootstrap.sh` 运行时 gate 已标注「TLS/errno & _meuos_exc NOT verifiable (qemu-user cannot program TPIDRURO; needs qemu-system/real hw)」防误导。
- arm libc 仍「编译可用 + 真运行未核验（qemu-user 限制）」状态。
