# 待实现：i386 printf %d 跨函数 va_list 缺陷

## 背景
STATE.md §2 mcc 已知限制（i386）：printf %d 跨函数 va_list 传递时，Oload 从寄存器
间接加载与从 slot 直接加载行为不一致，导致 vformat 收到错误值。根因是 QBE 的
Oload 对 slot 做 `movl slot(%ebp)`（直接加载）而对寄存器做 `movl (%reg)`
（间接加载）。

## 目标
修复 i386 va_arg 跨函数传递，使 `printf("%d", n)` 在 va_list 跨函数时返回正确值。

## 影响范围
- mcc `src/target/i386/i386_sysv.c`：selvaarg/selvastart 的 Oload/Ostorew。
- 可参考 zakaryan2004/qbe 的 i386 后端实现。

## 验收
- i386 上 `vprintf("%d %d %d", ...)` 经 `va_list *` 跨函数传递返回正确值。
- 现有 i386 回归不退化。
