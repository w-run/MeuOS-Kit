# meuos-sysroot msysctl `execlp` 隐式声明编译错

> 状态：🔄 待修复
> 发现：2026-08-05（libc-worker，libc-work 建基线时）
> 位置：`src/msysctl/main.c:342`
> 关联：meuos-libc build 依赖链（libmsys.a 已产出，不影响 mcc/libc）

## 现象

`make -C projects/meuos-sysroot` 在 `build/obj/msysctl/main.o` 编译失败：

```
src/msysctl/main.c: In function 'cmd_hist_diff':
src/msysctl/main.c:342:17: error: implicit declaration of function 'execlp'; did you mean 'execvp'?
  342 |                 execlp("diff", "diff", "-u", f1, f2, (char *)NULL);
```

## 根因

`meuos-sysroot` 编译统一带 `-std=c11 -D_POSIX_C_SOURCE=200809L -Werror`（可重现/严格 POSIX 模式）。`execlp` 属 XSI 扩展，在 `_POSIX_C_SOURCE=200809L` 下 `<unistd.h>` 不声明它 → GCC 报隐式声明（-Werror 中止）。与 `mt` 的 EOVERFLOW 缺陷（mt-eoverflow-build）同类：严格 `_POSIX_C_SOURCE` 下面向宿主 glibc 的声明缺口。

## 影响

仅 `msysctl` 工具构建失败。`libmsys.a`（`build/obj/libmsys/*.o` + `ar rcs`）在 msysctl 之前已成功产出，mcc 链接只依赖 `libmsys.a`，故 **不阻塞 mcc/meuos-libc** 构建链。

## 候选修复（本趟不修，避免范围蔓延）

1. `msysctl/main.c` 前 `#define _DEFAULT_SOURCE`（最简，让 unistd.h 放开 XSI 声明）。
2. 或改用 `execvp`（不涉及 XSI）等价实现：
   `char *argv[] = {"diff","-u",f1,f2,NULL}; execvp("diff", argv);`
3. 或按 mt-eoverflow 修法，仅对该 TU 放开 `_XOPEN_SOURCE`。

> 由 mt-eoverflow-build 同款严格模式缺陷引发的构建问题可归为「宿主 glibc 严格 `_POSIX_C_SOURCE` 声明缺口」一类，后续可统一定策略。
