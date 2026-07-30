# meuos-libtui — 终端 UI 库

> 为 MeuOS Kit 组件提供纯 C11 的终端 UI 支持。零第三方依赖，仅依赖 POSIX termios 和 ANSI/XTerm 转义序列。
> 提供分层 API：**底层原子操作 → 可组合布局树 → 显示模式模板 → 交互式组件**，
> 覆盖全屏、弹窗、向导、分栏、对话框、列表、输入框等场景。
> 适用：grub、shell、packagemanager、build、download、config、setup、install、AI agent、editor、reader、debug tool、remote connect 等全部 MeuOS 场景。

## 设计目标

- **轻量**：纯 C11 + POSIX，不依赖 ncurses/readline/terminfo
- **零依赖**：libtui.a 是单静态库，链接终端即可用
- **分层抽象**：底层 atom API → 布局树 → 模板模式 → 交互式组件
- **可组合**：所有组件基于 `tui_render_fn` 回调，可用作布局树叶子
- **主题化**：MeuOS 绿色调色板，组件可自由定制颜色

## 当前能力

### 底层 API

| 能力 | 状态 | 说明 |
|------|------|------|
| 原始模式 (raw mode) | ✅ | 终端 raw/cooked 模式切换 |
| 光标定位 | ✅ | 行列绝对/相对、保存/恢复、显隐 |
| 颜色/样式 | ✅ | 16 色前景/背景、属性设置 |
| 屏幕清除 | ✅ | 全屏/行/行尾清除 |
| 终端尺寸查询 | ✅ | ioctl(TIOCGWINSZ) |
| 输入解析 | ✅ | ASCII + 完整 CSI/SS3 转义序列 |
| 信号处理 | ✅ | SIGWINCH 回调 |
| 备用屏幕 | ✅ | 切换/恢复 |
| 超时输入 | ✅ | `tui_getkey_timeout()` |
| 鼠标支持 | ✅ | XTerm SGR 模式 |

### 布局系统

| 能力 | 状态 | 说明 |
|------|------|------|
| VBox 垂直布局 | ✅ | 子节点垂直排列，支持权重分配 |
| HBox 水平布局 | ✅ | 子节点水平排列，支持权重分配 |
| 内边距 | ✅ | 每个节点四边 padding |
| 递归渲染 | ✅ | 布局树任意深度嵌套 |
| `tui_app_layout` 全套模板 | ✅ | header + content + statusbar 一键创建 |
| `tui_split_layout` 分栏模板 | ✅ | sidebar + content 分栏布局 |

### 显示模式模板

| 能力 | 状态 | 适用场景 |
|------|------|----------|
| `tui_layout_fullscreen` | ✅ | 编辑器、阅读器、reader、debug tool |
| `tui_layout_centered` | ✅ | 弹窗、dialog、wizard、install、setup |
| `tui_layout_wizard` | ✅ | 安装器向导、setup wizard、config 引导 |
| `tui_layout_dual` | ✅ | 包管理器、shell、AI agent、remote connect |

### Widget 组件

| 组件 | 状态 | 适用场景 |
|------|------|----------|
| 边框面板 (Panel) | ✅ | 信息展示、分组、装饰性布局 |
| 进度条 (Progress) | ✅ | build、download、install 进度反馈 |
| 旋转器 (Spinner) | ✅ | 异步等待、AI agent 推理、任务加载 |
| 状态栏 (StatusBar) | ✅ | 全局状态、帮助提示、位置信息 |
| 标签 (Label) | ✅ | 文本/标题展示，支持对齐和样式 |
| 分隔线 (Separator) | ✅ | 视觉分区，支持标签标记 |
| 徽章 (Badge) | ✅ | 状态标记、版本号、tag |
| 表格 (Table) | ✅ | 结构化数据、包列表、配置项、调试信息 |
| 可选择列表 (List) | ✅ | 菜单导航、文件列表、包选择、命令历史 |
| 对话框 (Dialog) | ✅ | 消息/确认/警告/错误/输入，全场景交互 |
| 文本输入 (Input) | ✅ | 搜索框、命令输入、密码输入、配置字段 |

### 待实现 (P3+)

| 功能 | 状态 |
|------|------|
| 24-bit 真彩色支持 | ⏳ |
| 树形控件 (Tree) | ⏳ |
| 滚动文本视图 | ⏳ |
| 快捷键绑定工具 | ⏳ |

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

所有组件接受 `tui_color_t` 参数，可覆盖默认颜色。

## 目录结构

```
meuos-libtui/
├── ARCHITECTURE.md
├── Makefile
├── include/meuos/
│   └── libtui.h          # 公共 API
├── src/
│   ├── terminal.c        # 终端 I/O (raw/alt/mouse/signal)
│   ├── screen.c          # 屏幕操作 (光标/颜色/清除)
│   ├── input.c           # 输入解析 (key/event/text input widget)
│   ├── layout.c          # 布局树系统 + 显示模式模板
│   ├── widget.c          # widget: panel/label/separator/badge/progress/spinner/statusbar/table
│   ├── list.c            # 可选择列表 (keyboard navigation)
│   └── dialog.c          # 对话框 (info/warning/error/question/input)
├── test/
│   └── test.c            # 回归测试 (126 tests)
└── build/
```

## API 层析

### L0: 底层原子操作

```c
tui_raw_mode(0, 1);           // 进入原始模式
tui_cursor_goto(0, 5, 10);    // 光标定位
tui_set_fg(0, TUI_COLOR_GREEN);
tui_printf(0, "Hello");
tui_reset_style(0);
tui_getkey(0, &ev);           // 读取按键
```

### L1: 布局树

```c
tui_layout_t *root = tui_layout_vbox(1);
tui_layout_t *panel = tui_panel_new("Output", my_content, NULL);
tui_layout_add(root, panel, 1);
tui_rect_t area = { 1, 1, 24, 80 };
tui_layout_render(0, root, area);
```

### L2: 显示模式模板

```c
// 全屏（editor/reader）
tui_layout_t *fs = tui_layout_fullscreen(content_fn, data);

// 居中弹窗（dialog/wizard/install）
tui_layout_t *ct = tui_layout_centered(60, 12, content_fn, data);

// 向导（setup/config）
tui_layout_t *wz = tui_layout_wizard("Setup Wizard", content_fn, data, "Step 2/5");

// 分栏（shell/package-manager/ai-agent/remote-connect）
tui_layout_t *dl = tui_layout_dual(24, "Navigation",
                                   side_fn, side_data,
                                   content_fn, content_data);
```

### L3: 交互式组件

```c
// 列表选择
tui_list_t *list = tui_list_new(items, 10);
tui_list_render(0, &area, list);
tui_list_handle(list, &ev);                    // 键盘导航
int sel = tui_list_selected(list);

// 文本框输入
tui_input_t inp = { .active = 1, .prompt = "search> " };
tui_input_render(0, &area, &inp);
tui_input_handle(&inp, &ev);                   // 键盘输入
if (tui_input_done(&inp)) { /* 获取 inp.buffer */ }

// 表格
tui_table_t tbl = { .ncols=3, .nrows=20, ... };
tui_table_render(0, &area, &tbl);
tui_table_handle(&tbl, &ev);                   // 上下选择行

// 对话框
int result = tui_dialog_blocking(0, screen, "Confirm",
                                 "Delete?", TUI_DLG_QUESTION,
                                 TUI_DLG_YES | TUI_DLG_NO);
// or: 嵌入布局树
tui_layout_t *dlg = tui_dialog_layout("Warning", "Disk full",
                                      TUI_DLG_WARNING, TUI_DLG_OK);
```

## 设计原则

- **所有函数返回 `int`**：0 成功，负值错误码
- **无全局状态**：除 termios 备份和 SIGWINCH 外无全局变量
- **IO 通过 `int fd`** 参数指定，支持重定向
- **命名空间前缀 `tui_`**
- **布局树内存由 `tui_layout_free` 递归释放**，包括内部 widget 数据
- **交互式组件（list/dialog/input）使用 handle 模式**：渲染不阻塞、事件驱动，便于集成到事件循环
