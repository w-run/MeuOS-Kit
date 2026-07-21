# x86_64 target

首期架构实现目录，按以下边界拆分：

- `as_encode.c`：汇编指令编码；
- `ld_reloc.c`：x86_64 relocation 应用；
- `disas.c`：辅助工具反汇编；
- `abi.c`：节区/对齐/入口相关 ABI 规则。

这些文件暂未实现。P1 以 mcc 实际生成的汇编和 MeuOS libc 运行时为输入冻结
指令及 relocation 子集。
