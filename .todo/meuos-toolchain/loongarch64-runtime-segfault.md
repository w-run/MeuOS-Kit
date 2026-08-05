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
