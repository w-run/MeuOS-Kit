---
name: MeuOS 组件项目经验
description: meuos-libc C99、meuos-libtui、Shell/Utils 方向、自主特性方向、归并记录
type: project
---

# MeuOS 组件项目经验

## 1. meuos-libc C99 补全

2026-08-02 完成 C99 转换/格式化补全：strtof/atof/atol、div/ldiv/rand、mblen 家族、scanf 完整（%i/%o/%u + 长度修饰 + %f/%e/%g 浮点）、printf %a hex float（与 glibc diff 逐字节一致）、time.h 补 clock/difftime/asctime/ctime。strtod 已解禁 i386。

**阻塞项（mcc 工具链限制）**：
1. mcc 无 long double → strtold/%Lf/%La 无法实现
2. mcc i386 后端 `Kl op flagislt/flagiult not yet supported` → i386 完整 libc 构建失败
3. mcc codegen：fscanf 块紧跟 fmemopen 块会导致 exit 挂死（独立文件正常）

## 2. meuos-libtui

`projects/meuos-libtui/`，纯 C11 + POSIX 零外部依赖 TUI 库。P0-P2 已完成（核心 API + 输入解析 + 信号/鼠标），P3+ 待实现（文本缓冲/行编辑/24-bit 真彩色）。链接 `libtui.a` + `#include <meuos/libtui.h>`。

## 3. Shell + Utils 现代优先哲学

**方向（2026-07-31 大喵明确）**：完全实现 meuos-shell(msh) + meuos-utils。**现代优先 + GNU 兼容层叠加**（`--classic`/`--gnu` 开关），**绝对禁止实现"又一个 GNU 工具"**。libutils.a 增加现代组件（color/progress/icons/table/json-output）；msh 真 POSIX sh 语法 + bash 扩展 + zsh 插件/主题 + YAML 配置 + 插件系统（precmd/preexec/chpwd）。零外部依赖。worktree-shell-utils 分支。

## 4. 自主特性优化方向（不做缝合怪）

用户 2026-08-03 要求 mcc/m++ 做自主差异化能力，不只标准覆盖：
1. 更多细粒度 -O 级别（-O1/-O3/-Os/-Oz）
2. 更简化的编译体验（CLI、默认行为）
3. 组件干净接口联动（前端/MIR/后端）
4. clang 风格错误消息（源码定位+caret+修复提示）
5. 更丰富的调试接口（DWARF、-g）
6. 更自由的产物控制（-S/-c/-E、路径控制）
7. 中/英双语（i18n）

## 5. 归并记录

- **4 分支归并（9742e2f）**：alice/diana/eve/chloe。memit.c TLS 以 chloe g_pic 为正版。**x86_64 MIR-native -fPIC GOT 回归修复（97d5467）**：新增 emit_global_addr()（`movq sym@gotpcrel(%rip)`），MMOP_CALL 对外部符号补 @plt。check-olevel 3 项差距（if-conv/cmov、叶函数帧指针、O1 内存常量传播）。
- **6 分支归并（5aa1154）**：eve-i18n/alice-cli/hazel-fp/diana-dwarf/chloe-memconst/bella-cmov。check-olevel 三差距全清零。**集成 bug（47f2cc0）**：main.c `-f/-fno-omit-frame-pointer` 赋值写反（-fomit 误设 g_force_fp=1 应 0）。
- **MIR-native 后端注意**：输出用 TAB 分隔（grep 需 `\s*` 容错）；MV_GLOBAL 有 isext 字段；g_use_mir_backend 默认=1，MCC_MIR_BACKEND=0 回 LIR；memit 帧指针 g_omit_fp = optlevel>=2 && !g_force_fp && !hascall && !hasrbp && !dynalloc && !vararg。
