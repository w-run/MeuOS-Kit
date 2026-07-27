# loongarch64 target

已完成。编码器 ~900 行，支持 LA64 完整指令集（整数/浮点/原子/TLS）。
链接器支持 15 种重定位类型（含 TLS LE 和 GOT）。详见 `src/target/loongarch64/encode.c`、
`src/target/loongarch64/apply.c`。
