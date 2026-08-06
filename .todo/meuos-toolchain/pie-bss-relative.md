# PIE 动态链接里 libc .bss 全局缺 R_X86_64_RELATIVE（线程控制崩）

> 状态：✅ 已闭环（2026-08-07 toolchain-pie-worker 回归门验证通过）
> 闭环 commit：`ed78880f`（mcc-dev）
> 回归门：`ld_pie_e2e.sh`（check-ld-pie）
> 关联 commit：无（独立于 DTV/TLS 的既有/新缺陷）

## 现象

- **任何 PIE（动态链接）**里调用 `thrd_create` → `__meuos_control_add` 的 `lock_controls()`（`atomic_exchange(&thread_controls_guard, 1)`，libc `src/thread/state.c:42`）→ **SIGSEGV**；
- 根因：**mt/ld 的 PIE 里，libc 静态 `.bss` 全局**（`thread_controls` / `thread_controls_guard`，state.c:17-18）**缺 `R_X86_64_RELATIVE` 重定位** → 访问地址错误。

## 判定

- **独立缺陷，非 DTV/TLS**（DTV/TLS 机制本身正确并已闭环）；
- **静态 exe 线程正常**（thrtls / C11 threads 全过），**仅 PIE 动态运行期崩**；
- `pienothread`（最小 PIE + thrd_create）可稳定复现。

## 影响

- 阻断"新线程访问 dlopen 模块 TLS"的**完整组合用例**（要求 PIE + pthread + dlopen 一起）；
- P0.3 核心（dlopen + DTV 跨模块 `__tls_get_addr`）本身已闭环，唯此组合被此 gap 阻塞。

## 范围

- **mt/ld**：为 PIE 的 libc `.bss` 数据符号（thread_controls / thread_controls_guard）补 `R_X86_64_RELATIVE` 重定位；
- 或 **libc**（PIE 适配）：thread 控制表改用 PIE 可重定位的访问方式。

## 验收

- PIE + `thrd_create` 运行正常（pienothread 复现用例转 PASS）；
- **PIE 里新建线程访问 dlopen 模块 TLS 的完整组合用例**通过；
- 不引入其它门禁/架构回归。

## 范围约束

- 由 exec-toolchain（mt/ld RELATIVE 补全）或 exec-libc（thread 表 PIE 适配）修复；doc-pm 只登记与追踪；
- 修复后经验沉淀到 `.agents/knowledge/`。
