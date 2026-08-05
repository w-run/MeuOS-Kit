# mcc 自带 libc `<stdlib.h>` 头 `wchar_t` 未定义即被用（libc 域缺口，备排）

## 状态
已登记备排（2026-08-06）。由 mcc-worker 指出，归属 libc 域。

## 现象
mcc 自带 libc 的 `<stdlib.h>` 里 `wchar_t` 没先定义就被 `mbtowc` 等函数声明使用（报 `wchar_t 未定义`），独立于 mcc 宽字面量修复。

## 影响
- 在 mcc 目标环境（`--specs=meuos` 等）某些 TU 里若只 `#include <stdlib.h>`（不含 `<stddef.h>`/`<wchar.h>`）就会触发。
- mcc-worker 建议规避：依赖宽字面量时先 `#include <wchar.h>` 或 `typedef int wchar_t`（LP64 平台 4 字节）。

## 根治方向
在 meuos-libc `include/stdlib.h` 保证 `wchar_t` 可见（引入 <stddef.h>，或确认 `wchar_t` 定义点）。待排期。
