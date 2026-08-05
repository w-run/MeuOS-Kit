# loongarch64 运行时崩溃/汇编缺口（矩阵门禁发现）

> 状态：🔄 开放（2026-08-05 runtime 矩阵门禁发现）
> 关联：mt/objcopy 矩阵 `test/rt_matrix.sh`；loongarch64 专项（本阶段收束，下一阶段深挖）
> 分支参考：fix/mt-work（矩阵部分已合 main）

## 现象（loongarch64 runtime 矩阵）

`check-qemu-rtmatrix-loongarch64` 下多数 C 特性程序崩溃或无法汇编：
- **segfault（exit 139）**：`rr_struct` / `rr_array` / `rr_ptr`；
- **as 拒绝（能编？）**：`rr_call` / `rr_global`（mt/as 或 mcc 产物）；
- **exit 0（错值）**：`rr_i64` / `rr_fp`；
- 仅 `rr_arith`（`return 6*7`）PASS。

裸 `hello.c`（返回 42 常量）PASS，但带变量/结构体/全局/指针的程序崩溃——非单点 as 编码，是**运行时/内存寻址层**问题。

## 判定

- 排除 mt/as 单指令编码（hello + arith 过，且 mt/as 已清 5 个编码 bug：arm/aarch64/riscv64/i386）；
- 疑 **loongarch64 mcc 后向**（栈帧/结构体布局/全局寻址/函数调用约定）+ **mt/ld loongarch 链接**（重定位/动态）+ libc crt 综合；
- 与 i386/arm 排查法一致：先分离 `mcc -S` 产物语义 vs mt/as 寻址编码，GNU 交叉验证归属。

## 影响

- 高优先级：运行时崩溃面最大，loongarch64 程序几乎不可跑（除最简）；
- 但 loongarch64 非主目标架构，本阶段收束可后置。

## 范围

- 需拆 mcc 后向（mcc-worker 域）与 mt/ld 链接（mt 域）责任；
- 建议专项深挖，独立 worker；非快速 as 编码修。

## 验收

- loongarch64 runtime 矩阵多数/全部程序 PASS（不再 segfault）；
- 具体拆分待专项定位后更新此处。

## 范围约束

- 跨域：mcc loongarch 后端 + mt/ld 链接 + libc crt；
- mut 线下一阶段专项方向（2026-08-05 收束时登记）。

## 精确根因（2026-08-05 大块推进，mcc 域 CFG）

**mcc loongarch64 函数入口控制流布局错**：设局部变量基址的 setup block（`.Lmain.bb0`）被放到 .s **末尾且是死代码**，入口 prologue 后**直接 fallthrough 到 `.Lmain.bb1`**（用 a0 访问局部变量/结构体，a0 此时未初始化=调用者垃圾）→ 向非法地址写 → **segfault 139**。

证据（mcc loongarch64 产物，rr_struct）：
```
main:
  addi.d sp,sp,-32; st.d ra,sp,24; ...   # prologue
.Lmain.bb1:                               # ← 入口 fallthrough 直接执行
  addi.d t0,a0,0                          # 用 a0 写 struct.s.a —— a0 未初始化=垃圾地址
  st.w   t1,t0,0                          # 写非法地址 → SEGFAULT
  ... ret
.Lmain.bb0:                               # ← 设 a0=fp-24 的 setup block 在末尾，死代码
  addi.d t0,fp,-24; or a0,t0
  b .Lmain.bb1
```
rr_array/rr_ptr 同模式（局部数组/指针基址 bb0 在尾部死代码）。hello.c（`return 42` 无局部访问）不受影响故过。

已用 mt/as + GNU loongarch64 as 逐字节验证**各指令/分支编码正确**、mt/ld 链接正确 → **非 mt/as/ld，是 mcc 后端**：loongarch64 emit 时 entry 缺 `b .Lmain.bb0`（或 block 排序错，bb0 应前置/入口应跳它）。待 mcc-worker 修 loongarch64 后端 CFG/entry 跳转。

## 2026-08-05 CFG 闭环（mcc d142cbee）+ 剩 pcaddu12i LO12 约定

- mcc d142cbee 修 CFG entry（入口跳 `.L<fn>.bb0`）→ rr_struct/array/ptr/i64 **segfault 全移除转 PASS**。
- 剩 rr_call/global（as→现在能汇编，pcaddu12i 支持加入但函数地址 LO12 约定未全对，运行时 timeout/segfault）+ rr_fp（exit0）。
- mt 侧（我）：加 **pcaddu12i** encode（1RI20 opcode 0x1C）；apply.c **PCALA_HI20(71) 保留 opcode 高字节**（原硬写 0x1A 会破坏 pcaddu12i）。
- **重要**：PCALA_LO12(72) 保持 **绝对 S+A** 约定（Subtract-P 会让所有 loongarch 程序 startup segfault — 已验证回滚）。pcaddu12i 函数地址需要**另一种约定**（la/pcalau12i 用绝对，pcaddu12i 大模型可能真需 PC-相对）——**需区分，待专项**。
