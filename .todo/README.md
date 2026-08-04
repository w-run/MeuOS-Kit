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

### mcc

| 任务 | 状态 | 说明 |
|------|------|------|
| [aarch64-qemu-segfault](mcc/aarch64-qemu-segfault.md) | 🔄 | mcc 编译 aarch64 hello qemu 运行 segfault(139)，疑栈帧序列/crt1-AAPCS |

### meuos-libc

| 任务 | 状态 | 说明 |
|------|------|------|
| [fp-fmt-negzero](meuos-libc/fp-fmt-negzero.md) | 🔄 | libc fp_fmt 负零符号位丢失（%f/%g 输出 0 应为 -0） |

### meuos-shell（msh）

| 任务 | 状态 | 说明 |
|------|------|------|
| [msh-bashcompat](meuos-shell/msh-bashcompat.md) | 🔄 | bash 兼容层完善（数组展开/`set -e`/shopt） |
| [msh-zero-warning](meuos-shell/msh-zero-warning.md) | ⏳ | 零警告编译（cmd_dispatch/plugin.c 警告） |

### meuos-toolchain

| 任务 | 状态 | 说明 |
|------|------|------|
| [mt-readelf-pie-dynamic](meuos-toolchain/mt-readelf-pie-dynamic.md) | ✅ | `mt/readelf -d` 对 PIE 误报 no dynamic section（已完成 commit 49349b2） |
| [check-qemu-skip-wrapper](meuos-toolchain/check-qemu-skip-wrapper.md) | ✅ | check-qemu-x86_64/i386 缺 SKIP 包装（no-op，基线 df962a0 已含 de49e414 修复） |
| [mt-ld-dynamic-section](meuos-toolchain/mt-ld-dynamic-section.md) | ✅ | mt/ld 生成的 .dynamic 节区 sh_type 应为 SHT_DYNAMIC、sh_link→.dynstr（完成 commit 0015271e） |
| [rtld-e2e-pie-verify](meuos-toolchain/rtld-e2e-pie-verify.md) | ✅ | rtld e2e 实跑需合并 rtld-p0 后由 exec-integration 门禁覆盖（完成，聚合分支 2d4b65a 验证） |

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
