# i386 有符号 byte 本地常量折叠丢失符号（#22b，字节/短类型窄化族）

**状态**：🔶 open（登记 2026-08-06；#22a 修复验证时新发现，独立于 leal-0/MOVZX 缺陷）

## 症状
i386 上 `int f(void){ signed char x = -56; return x; }`（本地 signed char 常量）返回 **200** 而非 **-56**。
运行时会话内的 signed char 参数 `int g(signed char x){ return x; }` 传 `g(-56)` 返回 -56 **正确**——所以是**常量折叠/本地窄化**路径丢符号，非运行时 sext 路径。

## 根因方向
MIR 里 `signed char x = -56; return x;` 被折叠成：
```
%v9 = sar (i32) %v7, 24    ; 把 -56 归一到字节可表示位
store (i8) %v9, %v1
%v11 = load (i8) %v1
%v10 = sext (i32) %v11      ; 符号扩展
%v12 = sext (i32) %v10      ; 二次 sext（已 i32）
ret %v12
```
asm 最终只输出 `movl $200, %eax`（-56 的字节值 200 = 0xC8），**丢掉了符号扩展**（应为 `movl $0xFFFFFFC8` 或 `movsbl` 后符号扩展）。疑似常量折叠把 signed byte 常量缩窄成无符号低 8 位（200），二次 `sext(i32)` 在折叠层被当无操作忽略。

## 验证
- `int f(void){ signed char x=-56; return x; }` → qemu 返 200（错）。
- `int g(signed char x){ return x; }` `g(-56)` → 返 -56（对）。
- unsigned 对应 `unsigned char x=200; return x;` → 200（对，#22a 已修后）。
- 影响：本地 signed char/short 常量（写死负值）返回符号错。

## 关联
- #22a（byte 窄化返回 leal-0/MOVZX 落 LEA）已修（commit `7b907823`），本项是**有符号 byte 常量折叠**独立缺陷，不在 #22a 范围。
- 排查方向：常量折叠/窄化 pass（`mir/passes.c` msimp/fold 或 irgen const-int 窄化）在缩窄 signed byte 时丢符号；二次 `sext(i32 of i32)` 折叠误删。
