# meuos-sysroot msysctl `execlp` 隐式声明编译错

> 状态：🔄 待修复
> 位置：`projects/meuos-sysroot/src/msysctl/main.c:342`
> 发现：2026-08-05（libc-worker 建基线时）

## 现象
`make -C projects/meuos-sysroot` 在 `msysctl/main.o` 失败：
`error: implicit declaration of function 'execlp'; did you mean 'execvp'?`
（`-std=c11 -D_POSIX_C_SOURCE=200809L -Werror` 严格模式）。

## 影响
仅 `msysctl` 工具。`libmsys.a` 在其前已成功产出，mcc 链接只依赖 `libmsys.a`，
**不阻塞 mcc/meuos-libc** 构建链。

## 候选修复
1. `main.c` 前 `#define _DEFAULT_SOURCE`（最简放开 XSI 声明）。
2. 改 `execvp`（非 XSI）：`char *a[]={"diff","-u",f1,f2,0}; execvp("diff",a);`
3. 按 mt-eoverflow-build 同款策略仅对该 TU 放开 `_XOPEN_SOURCE`。
> 与 mt 严格 `_POSIX_C_SOURCE` 声明缺口同一类，可统一策略。
