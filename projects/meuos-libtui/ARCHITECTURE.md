# meuos-libtui — 终端 UI 库

> 为 MeuOS Kit 组件提供纯 C11 的终端 UI 支持。零第三方依赖，仅依赖 POSIX termios 和 ANSI/XTerm 转义序列。
> 提供**模板化布局系统**和**可复用 widget**，支持快速搭建现代化 TUI 界面。

## 设计目标

- **轻量**：仅提供终端操作原语和可复用布局模板
- **零依赖**：纯 C11 + POSIX，不依赖 ncurses/readline/terminfo
- **可组合**：布局树 + 渲染回调，支持递归嵌套，高层组件自由组合
- **双模式**：既提供底层原子 API，也提供模板化布局（`tui_app_layout`/`tui_split_layout`）
- **主题化**：MeuOS 绿色调色板，使用 ANSI 16 色系统

## 当前能力

### 底层 API (P0-P2)

| 能力 | 状态 | 说明 |
|------|------|------|
| 原始模式 (raw mode) | ✅ | 终端 raw/cooked 模式切换 |
| 光标定位 | ✅ | 行列绝对/相对定位、保存/恢复、显隐 |
| 颜色/样式 | ✅ | 16 色前景/背景、属性设置 |
| 屏幕清除 | ✅ | 全屏/行/行尾清除 |
| 终端尺寸查询 | ✅ | ioctl(TIOCGWINSZ) |
| 输入解析 | ✅ | ASCII + 完整 CSI/SS3 转义序列 |
| 信号处理 | ✅ | SIGWINCH 回调 |
| 备用屏幕 | ✅ | 切换/恢复 |
| 超时输入 | ✅ | `tui_getkey_timeout()` |
| 鼠标支持 | ✅ | XTerm SGR 模式 |

### 布局系统 (P0)

| 能力 | 状态 | 说明 |
|------|------|------|
| VBox 垂直布局 | ✅ | 子节点垂直排列，支持权重 |
| HBox 水平布局 | ✅ | 子节点水平排列，支持权重 |
| 内边距 | ✅ | 每个节点可独立设置四边 padding |
| 递归渲染 | ✅ | 布局树任意深度嵌套 |
| `tui_app_layout` 全套模板 | ✅ | header + content + statusbar 一键创建 |
| `tui_split_layout` 分栏模板 | ✅ | sidebar + content 分栏布局 |

### Widget 组件 (P0)

| 能力 | 状态 | 说明 |
|------|------|------|
| 边框面板 (Panel) | ✅ | 单线/双线/圆角/粗边，带标题 |
| 进度条 | ✅ | 标签+进度条+百分比，可定制颜色 |
| 旋转器 (Spinner) | ✅ | Braille 字符动画 |
| 状态栏 | ✅ | 左/右对齐文本，主题色背景 |
| 填充矩形 | ✅ | 背景色填充 |
| 样式文本 | ✅ | 前景+背景+属性组合 |
| 颜色输出 | ✅ | `tui_cprintf` / `tui_hline` |

### 待实现 (P3+)

| 功能 | 状态 |
|------|------|
| 文本缓冲 + 行编辑原语 | ⏳ |
| 24-bit 真彩色支持 | ⏳ |
| 表格组件 | ⏳ |
| 菜单/选择列表 | ⏳ |

## 主题

MeuOS 默认主题 `tui_meuos_theme` 使用 ANSI 绿色 (32) 作为主色调：

```
accent    = TUI_COLOR_GREEN   (主色调)
border    = TUI_COLOR_GREEN   (边框)
highlight = TUI_COLOR_GREEN   (高亮)
success   = TUI_COLOR_GREEN   (成功)
warning   = TUI_COLOR_YELLOW  (警告)
error     = TUI_COLOR_RED     (错误)
info      = TUI_COLOR_CYAN    (信息)
```

其他组件可通过 `tui_set_fg`/`tui_set_bg` 自由定制，无需使用默认主题。

## 目录结构

```
meuos-libtui/
├── ARCHITECTURE.md
├── Makefile
├── include/meuos/
│   └── libtui.h          # 公共 API
├── src/
│   ├── terminal.c        # 终端 I/O
│   ├── screen.c          # 屏幕操作
│   ├── input.c           # 输入解析
│   ├── layout.c          # 布局树系统
│   └── widget.c          # 面板/进度条/旋转器/状态栏
├── test/
│   └── test.c            # 回归测试 (83 tests)
└── build/
```

## API 使用示例

### 底层 API

```c
tui_raw_mode(0, 1);           // 进入 raw mode
tui_cursor_goto(0, 5, 10);    // 光标定位
tui_set_fg(0, TUI_COLOR_GREEN);
tui_printf(0, "Hello");
tui_reset_style(0);
tui_raw_mode(0, 0);           // 恢复
```

### 模板化布局

```c
// 内容区域渲染回调
int my_content(int fd, const tui_rect_t *area, void *data) {
    tui_cursor_goto(fd, area->row + 1, area->col + 2);
    tui_write(fd, "Hello from content");
    return TUI_OK;
}

// 一键创建完整应用布局：header + content + statusbar
tui_layout_t *app = tui_app_layout(
    "  MeuOS App  ",       // header 标题
    my_content, NULL,      // 内容回调
    "READY | Ln 1",        // 状态栏左
    "v1.0"                 // 状态栏右
);

// 渲染
tui_rect_t area = { 1, 1, 24, 80 };
tui_layout_render(0, app, area);

// 释放
tui_layout_free(app);
```

### 面板嵌套

```c
tui_layout_t *root = tui_layout_vbox(1);

// 带标题的面板
tui_layout_t *panel = tui_panel_new("Output", my_content, NULL);
tui_layout_add(root, panel, 1);    // weight 1 (fill remaining)

// 状态栏
tui_statusbar_t sb = { .left = "ready", .right = "F1=help" };
sb.bg = TUI_COLOR_GREEN;
sb.fg = TUI_COLOR_WHITE;
tui_layout_t *bar = tui_layout_leaf(tui_statusbar_render, &sb);
tui_layout_add(root, bar, 0);    // weight 0 (auto size)
```

## 设计原则

- **所有函数返回 `int`**：0 表示成功，负值表示错误码
- **无全局状态**：除 termios 备份和 SIGWINCH 处理外无全局变量
- **输入输出通过 `int fd`** 参数指定
- **命名空间前缀 `tui_`**
- **布局树所有权由用户管理**：`tui_layout_free` 递归释放整棵树
- **Widget 内存由布局节点持有**：`tui_layout_free` 会释放相关 widget 数据
