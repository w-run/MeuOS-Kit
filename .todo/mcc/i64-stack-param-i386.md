# i386 i64 栈传参缺陷（一致性专项 x86-i64param）

**状态**：🔶 open（登记 2026-08-06，mcc 一致性专项 task56 衍生）

## 症状
i386 上 `long long f(long long a, long long b)` 的形参（cdecl 栈传参，位于 `8(%ebp)`）被读成
`-1(%ebp)` / `3(%ebp)`，返回垃圾。（预存在缺陷，非常量槽位修复引入；常量路径同一底层不一致已修于 6008c405。）

## 根因
`projects/mcc/src/target/i386/i386_mabi.c::mabi_selpar`（~L183）i64 分支：
```
int lo = dst->slot;          // lowering 期还是 -1（未分配 slot 的 sentinel）
lov->slot = lo;  lov->reg = -1;
hiv->slot = lo + 4; hiv->reg = -1;
```
lowering 时 `dst->slot == -1`，被直接写进 LOAD 目标 `lov/hiv` 的 slot；RA（regalloc.c Phase B）之后
不会再给该 param 一个真实 slot → 消费端（emit_setccr/shift/add 等经 `i64_base`，i64_slot = slot+g_slot_base）
读 `-1(%ebp)`/`3(%ebp)`。

## 修复方向（未做，非快修）
需让 i64 param 在 lowering/RA slot 握手正确：要么 selpar 为 i64 param 预分配真实槽位（比照
alloca pad 的做法），要么消费端能识别 param 的真实栈地址 `[ebp+8+8i]`。涉及物化端（mabi_selpar）
与消费端（i64_base 系）约定，需 qemu-i386-static 复测。

## 验证/复现
`printf` 观察 `i64_add(0x1122334455667788LL, 1)` 返回值；`mcc -target i386 -S` 可见
`movl -1(%ebp),%eax`/`movl 3(%ebp),%eax`（param 读）。qemu-i386-static 用户态可跑。

## 规避
mcc 一致性专项的 `i386/test/i386/i64const.c` gate 刻意只用常量/局部，不碰 i64 栈传参，保持回归断言干净。
