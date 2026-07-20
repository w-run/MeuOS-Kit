# 待实现：-W / -w 诊断系统

## 背景
ARCHITECTURE.md §8 "Still pending"：`-Wall`/`-Wextra`/`-Werror`/`-w` 当前
被接受但是 no-op。Phase 1c 命令行现代化把它们解析并吞掉了。

## 目标
实现可配置的诊断系统：
- `-w` 抑制全部警告。
- `-Wall` / `-Wextra` 启用警告类别。
- `-Werror` 把警告升级为错误（影响退出码）。
- `-W<name>` / `-Wno-<name>` 单项开关。

## 影响范围
- 新增 `src/sema/warn.c`（或 `src/driver/diag.c`）：集中诊断发射 API。
- `include/mcc.h`：`struct decl` / `struct expr` 各处调用 `warn()`。
- `src/driver/main.c`：解析 `-W` 选项并设置全局诊断配置。

## 验收
- `-Wall` 对未使用变量、隐式函数声明等场景发出警告。
- `-Werror` 使这些警告以非零码退出。
