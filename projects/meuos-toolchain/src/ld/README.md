# ld

x86_64 静态链接器（P2 已完成）。实现文件：`link.c` + `main.c`。

支持 ET_REL + 归档读取、符号解析、PT_TLS + TPOFF32、-L/-l/-l:/--sysroot。
counter=2000 多线程程序端到端在 QEMU x86_64 运行通过。

动态链接在 P6 中实现。
