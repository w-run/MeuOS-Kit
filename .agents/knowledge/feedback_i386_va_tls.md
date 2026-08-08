# i386 runtime_va SIGSEGV 根因 + TLS 确认

## runtime_va SIGSEGV — 两个 emitter bug

**Bug 1** (`i386_memit.c:791`): MMOP_MOV 的 i64 路径守卫额外检查了 `s0->type == MT_I64`。MIR 常量类型为 `MT_I64`，导致 i32 MOV 使用常量源时错误进入 i64 路径，写入 8 字节溢出覆写相邻 va_list 指针槽，SIGSEGV。
- 修复：移除多余 `s0->type` 检查。

**Bug 2** (`i386_mabi.c:450`): va_list advance 用了 `mout_cst`，把常量放 `in->cst`，但 ADD handler 从 `in->src[1]` 读，cst 不可见 → `s1=NULL` → `mv_to_scratch(NULL,%ecx)` 发射 `xorl %ecx,%ecx`（advance=0），va_arg 取值全错。
- 修复：改用 `mout2` + `oaddr_imm`。

## i386 tls FAIL — 确认通过
TLS 代码生成正确（`@ntpoff` + `%gs:0`），单独运行 `tls.c` exit=0。