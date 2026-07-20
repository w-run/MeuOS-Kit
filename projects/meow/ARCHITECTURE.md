# meow - 目录结构与模块索引

> 本文档是 meow 构建系统的导航地图，供 AI agent 与人类阅读。
> 自举管线上下文见 `../../AGENTS.md` §3 与 `../../STATE.md`。

## 1. 概述

`meow` 是 MeuOS Kit 的原生构建系统，目标是取代 Make。`meow.yaml`
是原生构建文件；包获取、安装等只是可由构建目标表达的工作。Makefile
兼容模式仅作为旧软件迁移的过渡路径。

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
│   ├── parse.c              # parse_recipe() + 目标表/list 助手
│   ├── graph.c              # run_target() + 过期检测 + 命令展开
│   └── main.c               # main() 入口、参数分发、Makefile/bootstrap 兼容
└── test/
    └── make-compat/         # Makefile 兼容模式回归
```

## 3. 模块职责

| 模块 | 公开入口 | 职责 |
|------|----------|------|
| `state.c` | （仅全局变量） | `recipe_environment`、`targets[]`、`ntargets`、`default_target`、`parallel_jobs` 的唯一定义点 |
| `exec.c` | `run()`、`list_packages()` | 通过 `/bin/sh -c` 执行命令；枚举 `pkgs/` 下的包 |
| `recipe.c` | `load_recipe()` | 读取 `pkgs/<name>/meow.yaml`，处理根级 `include:` 单层片段拼接 |
| `parse.c` | `parse_recipe()`、`find_target()` | YAML 子集解析为 `targets[]`；目标表查询与 `%` 模式规则实例化 |
| `graph.c` | `run_target()` | 依赖图递归执行（含 `-jN` 并行 fork）、过期检测、自动变量 `$@/$</$^/$*` 展开 |
| `main.c` | `main()` | 参数解析、`build`/`clean`/`list`/`--bootstrap` 分发、Makefile 兼容降级 |

## 4. 数据流

```
main() --load_recipe()--> 原始 YAML 文本
       --parse_recipe()--> targets[] 表
       --run_target()----> 依赖图遍历 + expand_command() + run()
```

## 5. 全局状态策略

所有可变构建状态集中在 `state.c`，通过 `meow.h` 的 `extern` 声明暴露。
这避免了分散在各翻译单元的 `static` 全局导致状态分裂，便于未来
迁移到 per-build 上下文结构体（见 `src/.todo/`）。

## 6. 两套编译路径

- **宿主 CC**（`make check` 默认）：`-D_GNU_SOURCE`（为 `environ`）。
- **mcc + MeuOS sysroot**：`--specs=meuos --nostdlib`，链接 `libc-meuos.a`。
  `check-sysroot-static` 直接用 mcc 编译全部 `src/*.c`。
