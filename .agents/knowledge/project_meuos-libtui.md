---
name: meuos-libtui project initialized
description: Pure C11 TUI library for MeuOS Kit components, zero third-party dependencies
type: project
---

meuos-libtui 项目已在 `projects/meuos-libtui/` 下初始化 (worktree branch: `worktree-meuos-libtui`)。提供终端 I/O (raw mode/备用屏幕/鼠标/SIGWINCH)、屏幕操作 (光标/颜色/清除/尺寸) 和输入解析 (键盘序列/CSI 转义/SGR 鼠标/超时)。纯 C11 + POSIX 实现，零外部依赖。P0-P2 已完成 (核心 API + 输入解析 + 信号/鼠标)，P3+ 待实现 (文本缓冲/行编辑/24-bit 真彩色/辅助 UI 组件)。

**Why:** 为 MeuOS Kit 组件 (msh、meowctl、调试器等) 提供可复用的 TUI 支持，避免各组件重复实现终端操作代码。

**How to apply:** 其他组件需要 TUI 功能时，链接 `libtui.a` 并 `#include <meuos/libtui.h>`。继续开发在 worktree 中进行。
