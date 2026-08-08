# mbox — MeuOS Box: 统一开发沙箱

> 一个二进制，六架构原生，零外部依赖。MCP + WebPTY 双接口。

## 使命

```
mbox ./rootfs echo hello          # 自动读 rootfs/mbox.conf
mbox --loong64 ./rootfs /bin/sh   # CLI 覆盖 conf
mbox --mcp ./rootfs               # AI Agent 接口
mbox --webpty=8080 ./rootfs       # 浏览器终端
```

单一静态二进制，内建 6 架构 QEMU 用户态翻译器 + namespace/网络隔离 + MCP 接口 + WebPTY。

## CLI

```
mbox <arch> [options] ./rootfs [command]

架构: --x86_64 / --aarch64 / --riscv64 / --loongarch64 / --i386 / --arm

硬件/环境参数:
  --share-dir=host:guest    共享目录 (host path → guest mount point)
  --net=user                用户态网络 (SLiRP)
  --net=tap=br0             TAP 桥接
  --usb                     自动透传 USB (bus 透传)
  --cdrom=/path/to.iso      虚拟光驱

接口模式:
  --mcp                     启动 MCP 服务器 (Unix socket 或 stdio)
  --webpty=8080             启动 WebPTY 终端 (HTTP)
  --mcp-port=8080           同时启动 MCP HTTP 服务

rootfs 生成:
  mbox-mkrootfs --output=./rootfs/
    # 简单 shell 脚本: make 各组件 + cp 到目录
```

## mbox.conf

放在 rootfs 根目录，自动加载。JSON 格式：

```json
{
  "arch": "loong64",
  "net": "user",
  "share": [
    {"host": "/workspace", "guest": "/mnt/workspace"}
  ],
  "timeout": 300,
  "env": {
    "MEUOS_SYSROOT": "/sysroot"
  },
  "mcp": {
    "port": 8080
  },
  "webpty": {
    "port": 8081,
    "readonly": false
  }
}
```

CLI 参数优先级高于 mbox.conf。无 conf 无 arch 参数时默认宿主架构。

## 原理

不是包装 qemu-*-static 二进制，而是**源码层整合 QEMU 用户态翻译器**。如同 mcc 整合 cproc 前端 + QBE 后端后演进出自有 MIR/regalloc，mbox 在源码层嵌入 QEMU 用户态核心——一个二进制内建 6 架构的 ELF 加载 + 系统调用翻译能力。

```
mbox 内部 =
  1. 加载 rootfs/mbox.conf (若存在)
  2. 解析 CLI 参数 → 合并 conf
  3. 创建 namespace (unshare -m -p -f -n)
  4. 挂载共享目录 / 网络 / USB / cdrom
  5. 内建 QEMU 用户态 ELF 加载器
  6. 系统调用翻译 + 执行 (或 native 直通)
  7. 可选启动 PTY 复用器 → MCP / WebPTY
  8. 等待命令/接口完成 → 返回退出码
```

## PTY 复用器（核心架构）

所有接口共享同一个后端 PTM（伪终端复用器）：

```
                  ┌──────────────┐
    MCP Tools ◄───┤              │
                  │  PTY Muxer   ├──► 沙箱内 shell/进程
   WebPTY ◄───────┤              │
   (HTTP+WS)      └──────────────┘
```

- 一个沙箱会话对应一个 PTY muxer 实例
- MCP Tools 和 WebPTY 通过同一个 muxer 收发数据
- WebPTY 用 WebSocket 做实时双向流（输出回显 + 键鼠注入）

## MCP 接口

启动 `--mcp` 后，mbox 暴露以下 MCP Tools 给 AI Agent：

| Tool | 功能 |
|------|------|
| `sh` | 执行 shell 命令，返回 stdout/stderr/exitcode |
| `read` | 读取沙箱内文件 |
| `edit` | 写入/编辑沙箱内文件 (原子替换) |
| `stat` | 文件元信息 |
| `glob` | 文件模式搜索 |
| `grep` | 内容搜索 |
| `screen` | 获取当前终端屏幕内容（结构化文本，行缓冲） |
| `proc` | 进程列表/状态/信号 |
| `spawn` | 启动后台进程 (daemon) |
| `input` | 向 PTY 注入键鼠事件流 (keydown/keyup/mousedown/mousemove/mouseup)，用于自动化测试 Shell/TUI |

**不包含 git** — git 在宿主侧直接操作仓库。

所有操作在沙箱隔离环境中执行，不污染宿主。

## WebPTY

`--webpty=8080` 启动 HTTP 服务：

- **GET /** → 返回一个极简 HTML 页面（内嵌 WebSocket 客户端）
- **WebSocket /pty** → 实时双向流
  - 服务端 → 客户端：终端输出（ANSI 转义序列原样传输）
  - 客户端 → 服务端：键盘事件（keydown/keyup）+ 鼠标事件（mousedown/mousemove/mouseup）

HTML 页面不需要复杂框架——纯 JavaScript + Canvas 或 pre 元素。一行 HTML 配内联 script 即可。

## 设计原则

1. **源码整合** — QEMU 用户态源码嵌入 mbox，如同 mcc 整合 cproc+QBE 后演进出自有 IR。
2. **不自研 CPU 模拟器** — 用 QEMU 的 TCG，不自研指令模拟。
3. **单二进制** — `mcc --nostdlib --static -o mbox`，内建网络/Web/MCP。
4. **根目录 conf** — mbox.conf 是配置中心，保持 CLI 简洁。
5. **PTY 复用器** — 所有接口共享同一后端，MCP 和 WebPTY 数据同源。
6. **模块化 rootfs** — 一套 rootfs 所有架构共享。`mbox-mkrootfs` 是独立 shell 脚本。

## 实现顺序

| Phase | 内容 | 状态 |
|-------|------|------|
| 0 | `env/meuos-sandbox` shell MVP (验证概念) | ✅ |
| 1 | `projects/meuos-sandbox/` C 实现：mbox 基本框架 — 架构解析 + namespace + chroot + qemu-user 整合 + mbox.conf 加载 | ✅ |
| 2 | CLI 硬件参数: `--share-dir` `--net=user` `--cdrom` `--usb` | ✅ |
| 3 | PTY 复用器 + MCP 服务器 — sh/read/edit/screen/input 等 Tools | ✅ |
| 4 | WebPTY (HTTP+WebSocket) — 浏览器终端界面 | ✅ |
| 5 | 6 架构 QEMU 用户态深度整合。**轻量版完成**：增强 QEMU 自动检测（env/qemu/ → PATH → /usr/libexec/qemu/ → /usr/lib/qemu/）+ `qemu_available_mask()` + `qemu_version_info()`。**源码嵌入 QEMU 为远期目标**，当前调用宿主 `qemu-*-static` 二进制。 | ✅（轻量版）|
| S | `mbox-mkrootfs` shell 脚本 (make + cp, 不内嵌) | ✅ |