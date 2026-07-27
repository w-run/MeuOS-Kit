# gd-tls.md — i386 TLS 模型选择设计笔记

> 创建于 bug-i386-tls 修复之后，记录 TLS 模型选择的架构决策。

## 背景

i386 使用 TLS 变体 II（`%gs:0` 指向 TCB 末尾，TLS 偏移为负值）。
mcc 后端已实现全部三种 TLS 模型的重定位输出：

- `SThr` (LE): `@ntpoff` → `R_386_TLS_LE`
- `SExtThr` (IE): `@gotntpoff` → `R_386_TLS_IE`
- `SGenThr` (GD): `@tlsgd` → `R_386_TLS_GD`

## 修复后的模型选择逻辑

在 `main.c` 中，当 `--static` 且 `!pic` 且 `tls_model == TLSM_DEFAULT` 时，
自动降级为 `TLSM_LOCAL_EXEC`，确保所有 `_Thread_local` 访问使用 LE。

这样 extern `_Thread_local` 变量在静态构建中不再发出 IE 重定位，
而是使用 LE(`@ntpoff`)，静态链接器可直接处理。

## 参考

- `projects/mcc/src/driver/main.c` — tls_model 降级逻辑
- `projects/mcc/src/irgen/emit.c:192-212` — valref() TLS 模型选择
- `projects/mcc/src/target/i386/i386_emit.c:1569-1612` — i386 TLS 助记符发射
- C11 6.3.2.3p1 — 限定对象指针与 void* 转换
