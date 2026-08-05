# arm mt/as FAIL：as 无法组装 hello.c 的 arm 汇编

> 状态：✅ 已闭环（2026-08-05）
> 关联：修复 commit 546e5af（mt/as arm fp 别名 + 伪指令 no-op）+ a25cf3c（mcc arm_mabi.c MV_CONST），合入 main c72597e

## 现象

- `make check-qemu-arm`：**`FAIL: mt/as assembly`**（第一阶段 mcc 交叉编译 PASS，但 mt/as 组装 hello.c 的 arm 汇编失败）；
- 脚本在 as 阶段就 `exit 1`，QEMU runtime 未到，Makefile 用 `⚠️ SKIP (partial failure)` 包为非阻塞；
- mcc 已能成功生成 arm 汇编（`mcc -target arm -S` PASS），但 **mt/as 无法处理该 arm 汇编**。

## 判定

- **真实现象，非脚本误报**；
- mt/as 对 arm 的指令/伪指令/重定位支持有缺口（arm 后端已登记在 mgmt 进度，但 as 解析层可能未达 hello.c 产物所需覆盖）；
- 疑似根因方向：mt/as arm 后端缺某些指令编码、或 `.type`/`.size`/函数标签等 mcc 产物的 arm 伪指令未解析、或 arm 条件码/寄存器别名处理缺失。具体 error 文本在 as 报错输出中（需复现抓取）。

## 影响

- arm 跨架构 e2e（C 全管线）不能 PASS，仅 SKIP 非阻塞；
- 不影响 mt/as 已通过的 x86_64/i386 门禁；arm 是工具链 arm-multiver 支线的一部分。

## 范围

- **mt/as arm 前端**：补 mcc 产物 arm 汇编所需的指令/伪指令/重定位覆盖；
- 复现：`make check-qemu-arm` 即可复现，抓 as 对 hello.s 的具体报错。

## 验收

- `make check-qemu-arm` 至少走到 QEMU runtime 阶段（不再在 as 阶段 fail）；
- 若 QEMU runtime 仍失败，则单列新待办（arm 运行期）；
- 不引入 x86_64 及其它架构回归。

## 范围约束

- 由 exec-toolchain（mt/as arm 前端）修复；doc-pm 只登记追踪；
- 修复后经验沉淀到 `.agents/knowledge/`，本待办删除。
