# mt 全量 make EOVERFLOW 未声明（宿主 glibc 严格模式）构建失败

> 状态：✅ 已闭环（2026-08-07 toolchain-pie-worker）
> 修复 commit：`206c542c`（mcc-dev，archive.c 补 EOVERFLOW fallback）

## 现象

- `mt`（meuos-toolchain）**全量 make** 在**宿主 glibc 严格模式**下（如 `-D_POSIX_C_SOURCE=200809L`）编译 `src/ar/archive.c` 时 **`EOVERFLOW` 未声明** → 编译失败。
- 位置（已核实）：`src/ar/archive.c:59`（`set_error` 中 `case MT_AR_E_OVERFLOW: errno = EOVERFLOW; break;`）。
- glibc 严格模式下 `EOVERFLOW` 需在对应 feature-test-macro 下才由 `<errno.h>` 暴露；当前该文件虽 `#include <errno.h>`，但严格 `_POSIX_C_SOURCE` 下 `EOVERFLOW` 定义条件不满足。

## 判定

- **独立可重现构建缺陷**，**非 TLS P1 引入**：主工作区同样存在，只是此前未以 clean（强制全量重编）路径触发；P1c GD 集成的 clean 全量构建暴露了它。
- 与 GD/TLS 运行期逻辑无关，属构建系统/头文件可见性缺口，可独立修复。

## 范围

- `src/ar/archive.c`（EOVERFLOW 映射，推荐修复点：`_POSIX_C_SOURCE` 相关 feature-macro，或回退 `errno = EDOM/EINVAL` 等严格模式可见的 errno）；
- 或公共 `errno` 头（`include/mt/*.h`）统一放宽/补 feature-test-macro 声明；
- 补充：报告中曾提及 `src/nm/main.c:253-276`，**经核实当前源码该区域为 `print_entry_bsd`/`print_entry_posix` 纯 printf 输出，并无 EOVERFLOW 引用**，且 `nm/main.c` 未 `#include <errno.h>`；nm 侧是否另有 errno 依赖导致严格模式失败，需 exec 复核确认后再决定是否纳入本待办。

## 验收

- 宿主 glibc 严格模式（`_POSIX_C_SOURCE=200809L`）下 `make -C projects/meuos-toolchain` 全量通过；
- `make -C projects/meuos-toolchain check` 不引入回归；
- 明确 `ar` 组件 errno=EOVERFLOW 路径仍可正确触发（archive 解析溢出场景）。

## 范围约束

- 由 exec-toolchain 修复（构建/Makefile/src 头文件可见性），doc-pm 只登记与追踪；
- 修复后经验沉淀到 `.agents/knowledge/`。
