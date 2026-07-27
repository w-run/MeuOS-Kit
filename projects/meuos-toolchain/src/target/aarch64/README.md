# aarch64 target

已完成（超 P10 进度）。编码器 ~1250 行，支持 50+ 指令族含原子和浮点。
链接器支持 13 种重定位类型。端到端 as+ld+qemu-aarch64 测试通过。
详见 `src/target/aarch64/encode.c`、`src/target/aarch64/apply.c`。
