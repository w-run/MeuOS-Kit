# meuos-libtui — 终端 UI 库

> 为 MeuOS Kit 组件提供纯 C11 的终端 UI 支持。零第三方依赖，仅依赖 POSIX termios 和 ANSI/XTerm 转义序列。

## 设计目标

- **轻量**：仅提供终端操作原语，不包含重型 widget 框架
- **零依赖**：纯 C11 + POSIX，不依赖 ncurses/readline/terminfo
- **可组合**：原子化 API 可被高层组件（msh、meowctl、调试器）自由组合
- **跨平台**：Linux 优先，ANSI 兼容终端均可使用

## 当前能力

| 能力 | 状态 | 说明 |
|------|------|------|
| 原始模式 (raw mode) | ✅ | 终端原始模式设置/恢复 |
| 光标定位 | ✅ | 行列绝对/相对定位、保存/恢复 |
| 颜色/样式 | ✅ | 16 色前景/背景、粗体/下划线/闪烁等属性 |
| 屏幕清除 | ✅ | 全屏/行/行尾清除 |
| 终端尺寸查询 | ✅ | 通过 ioctl(TIOCGWINSZ) 获取行列数 |
| 输入解析 | ✅ | ASCII + 转义序列统一为 `enum tui_key` |
| 信号处理 | ✅ | SIGWINCH 窗口变化通知回调 |
| 回显输入 | ✅ | 单字符无回显输入模式 |
| 备用屏幕 | ✅ | 切换到/恢复备用屏幕缓冲区 |
| 超时输入 | ✅ | `tui_getkey_timeout()` 毫秒级超时 |
| 鼠标支持 | ✅ | XTerm SGR 鼠标模式启用/禁用 |

## 目录结构

```
meuos-libtui/
├── ARCHITECTURE.md        # 本文件
├── Makefile               # 构建 libtui.a
├── include/
│   └── meuos/
│       └── libtui.h       # 公共 API 头文件
├── src/
│   ├── terminal.c         # 终端 I/O (raw/cooked/备用屏幕)
│   ├── screen.c           # 屏幕操作 (光标/颜色/清除/尺寸)
│   └── input.c            # 输入解析 (键盘/转义序列/鼠标/超时)
├── test/
│   └── test.c             # 回归测试
└── build/                 # 构建输出
```

## API 设计原则

- **所有函数返回 `int`**：0 表示成功，负值表示错误码
- **状态通过参数指针传递**，不依赖全局变量（除内部 termios 备份外无全局状态）
- **输入输出通过 `int fd`** 参数指定，默认用 STDIN_FILENO/STDOUT_FILENO
- **命名空间**：`tui_` 前缀，避免符号冲突

## 组件依赖

meuos-libtui 是纯终端库，不依赖其他 MeuOS Kit 组件。仅需 POSIX 头文件：
- `<termios.h>` — 终端属性
- `<unistd.h>` — read/write/STDIN_FILENO
- `<sys/ioctl.h>` — TIOCGWINSZ
- `<signal.h>` — SIGWINCH
- `<time.h>` — 超时控制

## 路线图

| 阶段 | 功能 | 状态 |
|------|------|------|
| P0 | 核心API：原始模式/光标/颜色/清除/尺寸 | ✅ |
| P1 | 输入解析：完整键盘序列 + 超时 | ✅ |
| P2 | 信号处理 + 备用屏幕 + 鼠标 | ✅ |
| P3 | 文本缓冲 + 行编辑原语 | ⏳ 待实现 |
| P4 | 彩色输出 + 24-bit 真彩色支持 | ⏳ 待实现 |
| P5 | 进度条/旋转器（辅助 UI 组件） | ⏳ 待实现 |
