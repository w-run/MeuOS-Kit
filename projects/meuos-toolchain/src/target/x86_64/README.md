# x86_64 target

x86_64 架构已实现（P0-P2）。指令编码在 `src/as/assemble.c` 中，
重定位在 `src/ld/link.c` 中。后续按需拆分到独立的 target 文件。
