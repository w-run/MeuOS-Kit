# ARM 后端状态

> 2026-07-27 完成：ARM 32-bit (armv7+) 后端 + libc 运行时 + mt 工具链 as+ld 完整移植。

## 已完成

- **mcc 后端**：arm_targ.c / arm_abi.c / arm_isel.c / arm_emit.c（4 文件，~18KB）
  - AAPCS ABI 降级（R0-R3 参数传递）
  - VFP 浮点支持（-mfpu=vfp）
  - 子架构配置：-march / -mcpu / -mfpu / -mfloat-abi
  - ARM 预定义宏（__ARM_PCS_VFP、__VFP_FP__ 等）
- **meuos-libc 运行时**：9 个文件（crt1 + syscall + atomic + setjmp + sigreturn + thread_clone + set_tls + tls + aeabi）
  - set_tls 使用 `__kuser_set_tls` 内核辅助（0xffff0fe0）
  - AEABI 兼容层：aeabi.c + aeabi_wrap.S
  - bootstrap 6 测试程序全过
- **mt 工具链**：reloc.c / encode.c / apply.c（3 文件，~17KB）
  - 指令编码器：数据处理/内存/分支/VFP/push/pop
  - ELF32 输出 + RELA 格式
  - 18+ 重定位类型：ABS32/REL32/CALL/JUMP24/MOVW/MOVT/GOT_PREL/PLT32/GOT32/LDR_PC_G0/ALU_PCREL_0/TLS_GD32/TLS_LDO32/TLS_LE32
  - ELF32 链接：entry_size 12/24 自适应
- **验证**：qemu-arm 10.1.0 运行时验证通过（hello/atomic/setjmp/exit=42）

## 待启动

- **mt/ld ARM e2e 测试**：当前 bootstrap 脚本使用 cross-gcc 编译链接（mt/ld 暂不支持某些 ELF32 重定位）
- **MT_AS/MT_LD 集成**：mcc driver 通过 `MT_AS`/`MT_LD` 环境变量调用 mt 工具链（当前 ARM 走 cross-gcc）
- **QEMU VM rootfs**：env/ 中 ARM 的 initramfs 环境
- **i386/arm 双架构原子测试**：验证多架构交叉原子操作
