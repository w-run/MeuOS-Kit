# as

x86_64 汇编器（as-x86_64 已完成）。实现文件：`assemble.c` + `main.c`。

支持 mcc 生成的 AT&T 汇编子集（整数 + SSE/SSE2 标量）、ELF64 ET_REL 输出、
MeuOS libc 运行时汇编。编码与宿主 `as` 字节级一致（`test/as_sse_x86_64.sh`）。

多架构扩展（i386/aarch64/riscv64）在 target-i386...target-riscv64 中实现。
