# i386 target

已完成（超 P9 进度）。编码器 ~1120 行，支持 ModRM/SIB 内存寻址、条件跳转、
移位、and/or/div 等通用指令。链接器支持 ELF32 输入、R_386_32/PC32/PLT32
重定位。端到端 as+ld i386 ELF 验证通过。
详见 `src/target/i386/encode.c`、`src/target/i386/apply.c`。
