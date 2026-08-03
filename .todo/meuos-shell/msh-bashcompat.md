# msh-bashcompat — bash 兼容层完善

> **任务 ID**: `msh-bashcompat`
> **范围**: `projects/meuos-shell/src/exec/compat.c` + `src/var/`
> **依赖**: 无
> **状态**: 🔄 部分完成（compat.c 已有 `[[]]`/source/function 部分）

## 目标

在 POSIX sh 核心之上提供 bash 脚本兼容模式（`#!/bin/bash` 脚本可运行）。

## 现状（2026-08-04 实测）

- compat.c 已实现：`[[]]` 测试、`source`、`function` 关键字
- var/array.c 已实现 bash 风格数组（`arr=(...)`/`${arr[i]}`/`${arr[@]}`/`${#arr[@]}`）
- 缺：数组在展开/参数替换中的完整语义、`set -e`（errexit）精确行为、`shopt` 选项、`declare`/`local` 变量属性

## 验收

1. `#!/bin/bash` 脚本中的数组/`[[]]`/`source` 可运行
2. `set -e` 在命令失败时正确退出
3. `make check` 全绿，无回归
