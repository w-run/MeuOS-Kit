# riscv64 target

已完成（超 target-riscv64 进度）。编码器 ~940 行，支持 RV64I + M + A + F/D 完整指令集。
链接器支持 13 种重定位类型（含 TLS LE）。详见 `src/target/riscv64/encode.c`、
`src/target/riscv64/apply.c`。（注意：README 落后于实际代码，计划 2026-07-27 已实现。）
