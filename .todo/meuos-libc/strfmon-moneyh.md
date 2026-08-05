# 缺 strfmon / <money.h>（POSIX 货币格式化）

> 状态：🔄 待专项（P2，低优先）
> 发现：2026-08-05（libc-worker，跨线集成验收 mini-bc sweep）

## 现象
`strfmon` / `<money.h>` 在 meuos-libc **完全缺失**：无原型（`<money.h>` 不存在）、无符号。POSIX.1-2008 程序用 `strfmon`（`%i`/货币格式化）会 implicit declaration + 链接 undefined。

## 影响
少见面：仅用到货币格式化的程序（本地化/商务工具）受影响。普通工具（文字/文件/网络）不用。**P2 低优先**。

## 修复方向（可复用已实现的 localeconv）
1. 新增 `include/money.h`：`ssize_t strfmon(char *restrict, size_t, const char *restrict, ...);`
2. 新增 `src/strfmon.c`：基于 `struct lconv`（localeconv）实现 `%i`/`%n`/`[=flags][fieldwidth][.precision]` 子集，最小正确版即可（货币符号、小数点、指数分隔由 lconv 提供）。
3. 可选 `strfmon_l`/POSIX 变体。

## 约束
- 属 P2，不在此轮集成批次做。
- 真实程序若能避开 strfmon（如 printf 手工格式化）则不阻塞。
- 验证：编译含 strfmon 的探针 exit 0 + localeconv 返回的 lconv 字段被正确消费。

## 关联
- 已实现 `localeconv`/`setlocale`（locale.h，extern "C" 已包）；strfmon 直接复用。
- extern "C" 批次后，`<money.h>` 若有函数也需 `__BEGIN_DECLS` 包裹。
