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
| [const-fold-negzero](mcc/const-fold-negzero.md) | ✅ | mcc 常量折叠丢失负零符号（-0.0 折成正零）——根因为 x86_64 后端 fp_label 数值相等去重，已修 c004de8 |
| [static-global-array-segfault](mcc/static-global-array-segfault.md) | 🔄 | 静态 exe + 全局数组运行 segfault(139)——mcc/mt 的 R_X86_64_PC32/绝对数组寻址既有 bug（基线 07303b2 复现，非 TLS 引入） |
| [cpp-exception-runtime](mcc/cpp-exception-runtime.md) | 🔄 | C++ 异常运行期缺口：前端骨架已落地(9fc36de)，完整 catch+跨函数栈展开待后端 .eh_frame+landingpad 与运行时 unwinder+__cxa* ABI |
| [refactor-large-files](mcc/refactor-large-files.md) | 🔄 | 大文件分层重构：cpp_parse.c(10569)/link.c(4761) 等超大头文件拆小提 token 命中率——纯重构零行为改变（面向 lead-doc-mir-baseline 主线） |
| [exc-phase4-nontrivial-thunk](mcc/exc-phase4-nontrivial-thunk.md) | 🔄 | 异常4 增强：非 trivial 类 copy/dtor thunk 函数合成（用户拷贝构造/析构类真正携带对象）——需 mcc 前端 mkfunc+emit body 合成，脆前端风险，后置 |
| [exc-phase4-base-slice](mcc/exc-phase4-base-slice.md) | 🔄 | 异常4 增强：基派生切片（catch Base 捕获 throw Derived 子对象）——需 libc 把 _meuos_exc_throw_obj 的 offset 落地（f63bff1 留 void），libc 小增量，后置 |

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
| [mt-eoverflow-build](meuos-toolchain/mt-eoverflow-build.md) | 🔄 | mt 全量 make 在宿主 glibc 严格模式 `_POSIX_C_SOURCE=200809L` 下 EOVERFLOW 未声明（src/ar/archive.c:59，独立构建缺陷非 TLS P1 引入） |
| [mt-as-local-label](meuos-toolchain/mt-as-local-label.md) | 🔄 | mt/as 局部数字标签 `1f/2b` forward/backward 跳转目标解析错（`jne 1f` 跳错位置；手写 asm 受影响，mcc 产物用命名标签不受影响） |
| [pie-bss-relative](meuos-toolchain/pie-bss-relative.md) | 🔄 | PIE 动态链接里 libc 静态 .bss 全局（thread_controls/guard）缺 R_X86_64_RELATIVE → 线程控制崩(SIGSEGV)；非 DTV/TLS，独立缺陷，静态 exe 正常 |
| [mt-ld-pie-jmprel](meuos-toolchain/mt-ld-pie-jmprel.md) | 🔄 | mt/ld 构建的 PIE 主程序 JUMP_SLOT 入 .rela.dyn(DT_RELA) 非 .rela.plt(DT_JMPREL) → rtld_dlopen GOT 槽=0 崩；独立缺陷，host-ld 链可绕过，不阻塞方案 a |
| [i386-qemu-runtime-fail](meuos-toolchain/i386-qemu-runtime-fail.md) | ✅ | i386 QEMU runtime 返回 0 非 42——根因 mabi_selret i64 返回未处理 MV_CONST（return 42 读未初始化栈），已修 b6fb898/58af57a |
| [arm-as-assemble-fail](meuos-toolchain/arm-as-assemble-fail.md) | ✅ | arm mt/as 无法组装 mcc arm 产物——双层：mt/as 缺 fp 别名+伪指令 no-op + mcc arm_mabi.c MV_CONST，已修 546e5af/a25cf3c 合入 c72597e |
| [objcopy-o-format-gap](meuos-toolchain/objcopy-o-format-gap.md) | 🔄 | mt/objcopy 缺 `-O <format>`（ihex/srec/binary 输出），仅 ELF 节区操作，功能空白非缺陷 |

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
