# meow - 目录结构与模块索引

> 本文档是 meow 构建系统的导航地图，供 AI agent 与人类阅读。
> 自举管线上下文见 `../../AGENTS.md` §3 与 `../../STATE.md`。

## 1. 概述

`meow` 是 MeuOS Kit 的原生构建系统，目标是取代 Make + autoconf + pkg-config。
`.meow` 配方文件是原生构建文件（也兼容 `meow.yaml`）；包获取、安装等只是可
由构建目标表达的工作。Makefile 兼容模式仅作为旧软件迁移的过渡路径。

历史上 meow 是单个 `meow.c`（698 行），已按功能拆分为 `src/` 下的
多个翻译单元，每个文件对应一个职责域。

## 2. 目录树

```
meow/
├── Makefile                 # 简单 Makefile（AGENTS.md §4 禁止 autotools/cmake）
├── build/                   # 编译产物（gitignored）
├── ARCHITECTURE.md          # 本文件
├── src/
│   ├── meow.h               # 共享类型/常量/全局状态/跨文件原型
│   ├── state.c              # 全局状态定义（recipe_environment/targets/...）
│   ├── exec.c               # run() 命令执行器 + list_packages()
│   ├── recipe.c             # load_recipe() + 根级 include 解析
│   ├── parse.c              # YAML 配方解析器（向后兼容）
│   ├── parse_meow.c         # .meow 配方解析器 + 语义宏系统
│   ├── pkglib.c             # 内置库参数表（27 库，供 pkg-config 和 uses:）
│   ├── pkg_config.c         # pkg-config 子命令实现
│   ├── graph.c              # run_target() + 过期检测 + 命令展开 + 宏执行
│   ├── show.c               # meow show 配方信息展示
│   ├── init.c               # meow init 配方脚手架
│   ├── lint.c               # meow lint 配方语法检查
│   ├── probe.c              # 内联特性检测（autoconf 替代）
│   ├── triple.c             # 三元组解析（arch/subarch/vendor/os/abi）
│   ├── color.c              # ANSI 彩色输出 + meow_msg()
│   └── env.c                # meow env 环境概览
└── test/                    # 回归测试脚本
    └── make-compat/         # Makefile 兼容模式回归
```

## 3. 模块职责

| 模块 | 公开入口 | 职责 |
|------|----------|------|
| `state.c` | （仅全局变量） | `recipe_environment`、`targets[]`、`ntargets`、`default_target`、`parallel_jobs` 的唯一定义点 |
| `exec.c` | `run()`、`list_packages()` | 通过 `/bin/sh -c` 执行命令；枚举 `pkgs/` 下的包 |
| `recipe.c` | `load_recipe()` | 读取 `pkgs/<name>/project.meow`（降级 `meow.yaml`），处理 `include:` 片段拼接 |
| `parse.c` | `parse_recipe()`、`find_target()` | YAML 子集解析（向后兼容）；目标表查询与 `%` 模式规则实例化 |
| `parse_meow.c` | `parse_meow()` | `.meow` 配方解析 + 25+ 语义宏（run/env/download/has/lib/...）|
| `pkglib.c` | `find_lib()` | 内置 27 库的编译/链接参数表 |
| `pkg_config.c` | `cmd_pkg_config()` | `meow pkg-config` 子命令实现 |
| `graph.c` | `run_target()` | 依赖图递归执行（含 `-jN` 并行 fork）、过期检测、自动变量展开、宏触发 |
| `show.c` | `cmd_show()` | 配方信息展示 |
| `init.c` | `cmd_init()` | 自动生成 `project.meow` 配方 |
| `lint.c` | `cmd_lint()` | 配方语法检查 |
| `probe.c` | `probe_*()` | 编译/链接检测头文件/函数/库，生成 `config.h` |
| `triple.c` | `parse_triple_*()` | 三元组解析与推断 |
| `color.c` | `meow_msg()` | ANSI 颜色 + 分层输出 |
| `env.c` | `cmd_env()` | 构建环境概览 |

## 4. 数据流

```
main() --load_recipe()--> 原始 recipe 文本
       --parse_meow() / parse_recipe()--> targets[] 表 + uses[] + recipe_deps[]
       --expand_uses()-------------------> 注入 PKG_*_LIBS / PKG_*_CFLAGS 环境变量
       --probe_run()---------------------> config.h 生成
       --run_target()--------------------> 宏执行（unpack/patch/has/test/...）+ 命令展开 + run()
```

## 5. .meow 语义宏系统

| 类别 | 宏 | 说明 |
|------|----|------|
| **run 修饰符** | `run(!):` | 遇错中断（默认） |
| | `run(?):` | 遇错继续 |
| | `run(q):` | 安静输出 |
| **环境** | `env:` | 环境变量注入（支持引号） |
| | `toolchain:` | 交叉编译前缀（自动设 CC/AR/STRIP） |
| | `cflags:` / `ldflags:` | 编译/链接参数快捷声明 |
| **目录** | `srcdir:` / `builddir:` | 源码/构建目录 |
| | `workdir:` | 工作目录 |
| **依赖** | `download:` | 下载源 URL |
| | `sha256:` | SHA-256 校验 |
| | `unpack:` | 自动解压 tar.gz/xz/bz2/zip |
| | `patch:` | 补丁文件列表 |
| | `has:` | 宿主工具检测（command -v） |
| | `lib:` | 库依赖检测（自动 probe） |
| | `uses:` | 库编译/链接参数导入 |
| **构建** | `parallel:` | 每目标并行度 |
| | `test:` | 测试命令 |
| | `log:` | 构建日志文件 |
| **后处理** | `copy:` | 声明式文件复制 |
| | `strip:` | 自动删除调试符号 |
| **流程** | `only:` / `except:` | 架构白名单/黑名单 |
| | `pre:` / `post:` | 前置/后置钩子 |
| | `error:` | 失败回调 |
| **元数据** | `meta:` | 包描述等信息 |

## 6. 全局状态策略

所有可变构建状态集中在 `state.c`，通过 `meow.h` 的 `extern` 声明暴露。
这避免了分散在各翻译单元的 `static` 全局导致状态分裂，便于未来
迁移到 per-build 上下文结构体。

## 7. 两套编译路径

- **宿主 CC**（`make check` 默认）：`-D_GNU_SOURCE`（为 `environ`）。
- **mcc + MeuOS sysroot**：`--specs=meuos --nostdlib`，链接 `libc-meuos.a`。
  `check-sysroot-static` 直接用 mcc 编译全部 `src/*.c`。
