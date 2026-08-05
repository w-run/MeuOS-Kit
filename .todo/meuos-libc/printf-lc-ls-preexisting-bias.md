# 窄 printf：`%lc`/`%ls` 忽略 `l` 修饰符（预存在偏差，备排）

## 状态
已登记备排（2026-08-06，libc-worker）。不在本轮 wprintf 范围。

## 现象
窄版 `__meuos_vformat`（src/stdio/fmt_out.c）的 `%c`/`%s` case **忽略长度修饰符**：
- `%lc` 被当 `%c`（收 `int` 单字节）
- `%ls` 被当 `%s`（收 `char*` 窄串）

标准下 `%lc` 应收 `wint_t`（宽字符）、`%ls` 应收 `wchar_t*`（宽串）。

## 范围决定（team-lead）
不修，避免范围蔓延撞窄 printf 回归。待独立排期处理。

## 参考
宽版 wprintf（__meuos_wvformat，src/stdio/wprint.c）已正确处理 `%lc/%ls/%C/%S`，窄版若要对齐可参考。
