# 项目待办（.todo/）

> 所有**未完成的项目任务**统一存放在顶层 `.todo/<project>/`，按项目分子目录。
> 与全局记忆（`.agents/knowledge/`）是两个独立系统：
>
> | 系统 | 位置 | 内容 |
> |------|------|------|
> | **项目待办** | `.todo/<project>/<topic>.md` | 接下来要做什么（未完成任务） |
> | **全局记忆** | `.agents/knowledge/` | 已经学到的经验（缺陷闭环、纪律、修复方案） |
>
> - 待办**完成** → 删除 `.todo` 文件，经验沉淀到 `.agents/knowledge/`
> - 禁止在 `projects/<name>/.todo/` 下新建待办（统一在此）

## 索引

### meuos-shell（msh）

| 任务 | 状态 | 说明 |
|------|------|------|
| [msh-bashcompat](meuos-shell/msh-bashcompat.md) | 🔄 | bash 兼容层完善（数组展开/`set -e`/shopt） |
| [msh-zero-warning](meuos-shell/msh-zero-warning.md) | ⏳ | 零警告编译（cmd_dispatch/plugin.c 警告） |

### meuos-utils

| 任务 | 状态 | 说明 |
|------|------|------|
| [utils-tail-follow](meuos-utils/utils-tail-follow.md) | ⏳ | `tail -f` 跟随模式 |
| [utils-sed-multiline](meuos-utils/utils-sed-multiline.md) | ⏳ | sed 多行模式 |
| [utils-awk-varassign](meuos-utils/utils-awk-varassign.md) | ⏳ | awk 变量赋值 gsub 修复 |
| [utils-tar-xz](meuos-utils/utils-tar-xz.md) | ⏳ | tar xz/zstd 透传 |
| [utils-gzip-lz77](meuos-utils/utils-gzip-lz77.md) | ⏳ | gzip LZ77 压缩 |
| [utils-patch-context](meuos-utils/utils-patch-context.md) | ⏳ | patch context 格式 |
| [utils-unzip-create](meuos-utils/utils-unzip-create.md) | ⏳ | zip 创建能力 |

### mcc

> 2026-08-04 审计发现的 2 个缺陷（atomic 窄类型符号扩展 / TLS 局部静态符号不一致）已闭环修复，合入 mcc-toolchain `7ff1fc5`。详见 `worker-deployment.md` "修复闭环"小节。

### meuos-libc

| 任务 | 状态 | 说明 |
|------|------|------|
| [defect-fp-printf-negative](meuos-libc/defect-fp-printf-negative.md) | 🔴 新发现 | 2026-08-04：vfprintf `%.2f` 负数浮点输出 `0.00`（应 `-3.14`），46 个浮点 printf 失败。正数 `%f`/`%e` 正常 |
