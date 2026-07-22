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
- 后续：浮点/SSE 完整编码、多架构 ELF/CPIO、MeuOS QEMU sysroot 验证 `counter = 2000`、mcc driver 集成。
