# i386 有符号 byte 本地常量折叠丢失符号（#22b，字节/短类型窄化族）

**状态**：✅ 已闭环（2026-08-07 mcc-backend-campaign 验证，commit 3477e6a1）<br>MOVSX 源宽度从 in->dtype 改为 s0->type，signed char -56 正确符号扩展

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

## 根因（修正：2026-08-06 定位完成）
实际根因**不在** `mir/passes.c` 折叠链——msimp_block 对 `sext (i32) (i8)200` 的折叠结果正确（final 数组里源就是 `(i8)200`，符号扩展留给 emit）。
真正的 bug 在 `projects/mcc/src/target/i386/i386_memit.c` 的 `MMOP_MOVSX` emit：
原代码 `switch (in->dtype)` 用**目标宽度**（sext i8→i32 时 `in->dtype == MT_I32`）判断 movsbl/movswl，走 default 不生成符号扩展，直接把常量 200 当 i32 立即数返回，符号丢失。
其他 64 位后端（x86_64/arm/aarch64/riscv64/loongarch64）的 MOVSX 用 `in->dtype` 能工作是因为其机器 MIR dtype 语义不同（目标为 i64 扩展）；x86_64 已用 `s0->type`（源操作数宽度）取源宽度。
修复：i386 MOVSX emit 改为 `MType st = s0 ? s0->type : in->dtype;`，与 x86_64 一致，按源宽度发 movsbl/movswl。

## 修复
- `projects/mcc/src/target/i386/i386_memit.c`：`MMOP_MOVSX` 非 i64 分支按源操作数宽度（`s0->type`）发 movsbl/movswl，恢复符号扩展。
- `projects/mcc/test/i386/signed_byte_fold.c`：新增回归源（signed char 边界 ±、mixed unsigned）。
- `projects/mcc/test/i386/regress.sh`：新增 #22b 编译 gate（要求 movsbl 存在且 sb_neg56 不裸返 $200）。

## 验证
- `-O2` 全边界（±56/±1/±127/±128/0、unsigned 200）运行时 exit=0。
- `check-i386`（regress.sh）通过；`check-c99`/`check-c11`（x86_64 宿主）无回归。
- 修复前 gate 失败（no movsbl），修复后通过——gate 有效。

## 状态
✅ closed（已修并提交；i386 MOVSX 源宽度修复）
