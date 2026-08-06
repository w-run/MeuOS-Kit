# mt/ld 构建的 PIE 主程序 JUMP_SLOT 未入 .rela.plt（DT_JMPREL）致 rtld_dlopen 崩

> 状态：✅ 已闭环（2026-08-07）
> 闭环 commit：`604fcd9f`（mcc-dev，已合入 tmp/toolchain-pie-link 分支）
> 回归门：`ld_pie_e2e.sh`（check-ld-pie）

## 修复

`604fcd9f` + `ed78880f` 在 mt/ld 中：
1. 将 JUMP_SLOT 动态重定位写入 `.rela.plt`（DT_JMPREL）而非 `.rela.dyn`（DT_RELA）
2. PIE/共享模式下为 .data/.bss 数据符号生成 R_X86_64_RELATIVE

## 验证

- `ld_pie_e2e.sh`：DT_JMPREL 存在、.rela.dyn 不含 JUMP_SLOT、PIE 运行时 exit=42
- `rtld_e2e.sh`：PIE + ld.so 全链通过
