# C++ 异常运行期缺口（完整 catch + 跨函数栈展开）

> 状态：🔄 开放（2026-08-04 exec-mpp-lite P2-1 异常基础落地后登记）
> 关联 commit：`9fc36de`（mcc: cpp: basic exception handling，exec-mpp-lite）

## 现状（前端骨架 + IR 降级已落地）

- m++ `throw` **编译通过**并降到 runtime 调用（throw → `_meuos_exc_throw` runtime 调用）；
- `try/catch` **语法识别**但发清晰诊断：`'try'/'catch' requires the landingpad unwinder backend`；
- 即：前端基础已就位，**完整 catch + 跨函数栈展开需后端 + 运行时联动**，非 P2-1 本次可闭环。

## 缺口（三里程碑推进）

- **(a) 前端骨架 ✅**：throw/runtime 调用 + try/catch 识别（`9fc36de` 已含）；
- **(b) 后端 .eh_frame + invoke/landingpad**：
  - MIR/x86_64 后端：`invoke`/call-unwind + **landingpad 指令语义**；
  - **从零生成 .eh_frame**（DWARF CFI：CIE/FDE、CFA / 寄存器恢复规则）——当前 x86_64 后端**完全不产生 CFI**；
- **(c) 运行时 unwinder + __cxa\* ABI**：
  - rtld/libc：异常对象分配（`__cxa_allocate_exception`）；
  - `.gcc_except_table` 类型表 + `__gxx_personality_v0`；
  - 基于 .eh_frame 的栈展开器（`_Unwind_*`）：`__cxa_begin_catch` / `__cxa_end_catch` / rethrow；
  - rtld 需处理 `.eh_frame` / `.gcc_except_table` 段合并。

## 范围

- `projects/mcc`：后端（invoke/landingpad/.eh_frame CFI）+ 前端 catch 完整化（catch 类型匹配 dispatch、基类捕获、rethrow）；
- `projects/meuos-libc`：cxxabi（__cxa_\*）+ unwinder（_Unwind_\*）实现；
- `projects/meuos-toolchain`：rtld 段合并（.eh_frame + linker personality）。

## 验收

- 跨函数 throw+catch 运行 **exit 0**；
- rethrow 正确、基类捕获正确、栈展开正确；
- `make -C projects/mcc check` + `make -C projects/meuos-toolchain check` + `make -C projects/meuos-libc check` 全 PASS。

## 范围约束

- 由 exec-mcc-lite / exec-libc-lite / exec-toolchain-lite 按三里程碑推进（前端→后端→运行时），doc-pm 只登记与追踪；
- 里程碑 (a) 已含于 `9fc36de`；(b)(c) 分开闭环后分别更新状态；
- 修复/落地后经验沉淀到 `.agents/knowledge/`。
