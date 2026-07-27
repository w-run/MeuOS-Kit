# meuos-toolchain 工作日志

## 2026-07-22：P0 框架启动

- 使用独立 worktree：`/workspace/MeuOS-Kit-toolchain`。
- 分支：`work/meuos-toolchain`。
- 首期目标锁定为 x86_64 ELF64 little-endian。
- 建立 `include/mt`、`src/libelf`、`src/ar`、`src/target/x86_64` 及后续工具目录。
- 实现 `libelf` ELF64 头部/节区/symtab/strtab 接口和可复现 SysV/GNU ar 读写接口。
- P0b 完成：`/` symbol index、GNU `//` long-name table、`r` 替换、`q` 追加，宿主 ld 互操作通过。
- 未修改 mcc、meuos-libc、bootstrap.sh 或 aarch64 Agent 的文件。

## 协作约束

- 工具链代码只在 `projects/meuos-toolchain/**` 中提交。
- `projects/mcc/src/driver/host_toolchain.c` 和 `projects/mcc/Makefile` 是 P3 集成边界，待独立提交处理。
- 详细阶段、任务和验收门禁见 `ARCHITECTURE.md`。

## 2026-07-22：P0b ar 核心完成

- `ar rcs` 现在从 ELF64 `.symtab/.strtab` 生成 GNU `/` index；宿主 ld 可直接从 archive 抽取成员。
- 长成员名使用 GNU `//` table；`ar t/p/x` 可解析；`r` 替换同名成员，`q` 追加成员。
- `make -C projects/meuos-toolchain check` 已覆盖 ELF symbol、宿主链接、长名、追加、替换和解出。
- 下一阶段：P1 x86_64 assembler；mcc Makefile/driver 集成仍延后到 P3。

## 2026-07-22：P1/P2 x86_64 核心完成

- 新增 mt/as：解析 mcc 常见 AT&T x86_64 整数、栈、分支、调用、GOT/PLT、数据伪指令，生成 ELF64 ET_REL。
- 新增 mt/ld：合并 ET_REL、解析全局/弱/common、处理 ar 输入和 `.text/.rodata/.got/.data/.bss`，生成可运行 ET_EXEC。
- `test/ld_smoke.sh` 覆盖 mt as + mt ar + mt ld + GOT/PLT + syscall-only `_start`。
- `make -C projects/meuos-toolchain check` 全绿。
- 下一阶段是接入 MeuOS libc/crt1/sysroot；浮点/SSE、完整 GNU as 兼容和 mcc driver 集成仍未完成。

## 2026-07-22：x86_64 as runtime coverage 扩展

- 支持 C 风格块注释、GNU numeric labels（`1f/1b`）、SIB 地址、`lock xadd/cmpxchg`、`xchg`、`mfence`、`hlt`、`syscall`。
- 新增 `test/as_libc_x86_64.sh`，crt1、atomic、setjmp、sigreturn、thread_clone、syscall gate 六个 MeuOS libc x86_64 汇编文件均可由 mt/as 生成 ET_REL。

## 2026-07-22：ld sysroot 集成

- 加入按需抽取 `mt_ar_foreach`，并把归档移动到 `mt_ld_link` 后置的迭代 `extract_archives`，从而让 ld 自动从 `.a` 抽取被引用成员。
- 实现 R_X86_64_TPOFF32 relocation（`-(symbol_address)`），覆盖 MeuOS libc 中 `errno_value` 等静态 TLS 引用。
- `test/ld_sysroot.sh` 使用宿主 `/workspace/MeuOS-Kit/sysroot`：mcc 生成 `printf` 测试 → mt/as → mt/ld 与 `crt1.o` + `libc-meuos.a` + `libatomic-meuos.a` 链接为可在宿主 Linux 上输出 `toolchain = 42` 的可执行文件。

## 2026-07-23~27：P3-P4 多架构扩展

- P3（mcc driver 集成）：`host_toolchain.c` 通过 `MT_AS`/`MT_LD` 环境变量集成 mt 工具链，`check-mt-integration` 验证通过。
- P4（二进制辅助工具）：nm、readelf、strip、objcopy、objdump 全部实现，含 libelf/libdisasm 共享库。
- P5（自举验证）：`check-sysroot-static` 通过，sysroot 内 mcc+meow 自重建 Kit，`check-mt-integration` 零宿主 cc 依赖。
- P9（i386 架构）：编码器增强（ModRM/SIB/条件跳转/移位/and/or/div），ELF32 输入读取 + i386 重定位分派。
- P10（aarch64 架构）：mt/as 编码器 + mt/ld 重定位 + qemu 端到端验证通过。
- P11（riscv64 架构）：mt/as 编码器 + mt/ld 重定位 + TLS LE + qemu 端到端验证通过。
- loongarch64：mt/as 编码器 + mt/ld 重定位（15 类型，含 TLS LE/GOT）+ libc 运行时。
- arm：mt/as 编码器 + mt/ld ARM 重定位 + mcc 后端 + libc 运行时。
- .msys Phase 3：mt/ld 集成 `.msys` 单文件 sysroot（`--sysroot=<path>.msys` 自动解包）。
