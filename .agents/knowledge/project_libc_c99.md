---
name: meuos-libc C99 补全与 mcc 阻塞项
description: 2026-08-02 mxx-work 会话完成 meuos-libc C99 转换/格式化补全；记录 mcc i386/long double 阻塞项
type: project
---

2026-08-02 在 mxx-work worktree（worker-libc 角色）完成 meuos-libc C99 转换/格式化补全（11 个 libc 提交，见 git log）：

- strtof/atof/atol/atoll、div/ldiv/rand/srand、mblen 家族、wchar_t 移入 stddef.h
- fscanf/vfscanf/vscanf、scanf %i/%o/%u/%X + 长度修饰 %hd/%hhd/%ld/%lld + %f/%e/%g 浮点
- printf %a/%A hex float（与 glibc diff 逐字节一致）、%f/%e/%g 舍入位置修复
- time.h 补 clock/difftime/asctime/ctime；Makefile 修 .DEFAULT_GOAL
- strtod 已解禁 i386（原 #if !defined(__i386__) 排除，llabs 改位运算绕开 Kl flagislt）

**阻塞项（需 mcc worker 处理）：**
1. mcc 无 long double：`mcc: long double is not yet supported` → strtold/%Lf/%La 无法实现。
2. mcc i386 后端 `i386_emit.c: Kl op flagislt/flagiult not yet supported` → i386 完整 libc 构建失败（fmt_out.c/fp_fmt.c 崩溃，HEAD 亦如此）。i386-bootstrap.sh 用手工编译特定对象避开。
3. mcc codegen 缺陷：test/stdio.c 的特定栈布局下，fscanf（va_list 传递）块紧跟 fmemopen 块会导致程序 exit 时挂死；独立文件则正常。

**Why:** 这些是跨会话持续的编译工具链限制，决定哪些 libc 功能不可做。
**How to apply:** 遇到 strtold/%Lf 或 i386 构建相关任务时先确认 mcc 是否已支持再实施；fscanf 测试保持独立文件。
