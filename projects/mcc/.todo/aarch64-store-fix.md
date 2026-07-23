<!--
priority: P3
status: done
done_ts: 2026-07-23
note: 已修复（omap Ki 化 + isel store 特殊处理），留为永久参考
-->

# 待修复：mcc aarch64 后端存储到全局变量崩溃 — ✅ 已修复

## 背景
编译 aarch64 libc runtime 的 `src/arch/aarch64/tls.c` 时，mcc 在
`src/target/aarch64/aarch64_emit.c` 触发 `dying: invalid class` 断言，
阻塞 aarch64 完整 runtime 移植。

## 复现（修复前）
```c
/* tls.c 简化复现：静态全局 + 64 位赋值 */
static unsigned long entry_size;
void scan(unsigned long v) { entry_size = v; }
```
```
$ mcc --target=aarch64 -c tls.c
src/target/aarch64/aarch64_emit.c:126: dying: invalid class
```

## 根因诊断
两层缺陷叠加：

### 缺陷 1：`aarch64_emit.c` omap 存储条目类别错误
原始实现：
```c
{ Ostorel, Kw, "str %L0, %M1" },     /* BUG: cls=Kw 但 i->cls=Kl */
```
omap 匹配规则（参考 `emit.c` 的 `emitins`）：
```c
omap[o].cls == i->cls || omap[o].cls == Ka
  || (omap[o].cls == Ki && KBASE(i->cls) == 0)
```
当 `i->cls == Kl` 时：Kw 既不等于 Kl、也不等于 Ka 也不是 Ki，匹配失败 → die。

### 缺陷 2：`aarch64_isel.c` 缺少 store 特殊处理
`argcls(&i, 0)` 对 `Ostorel` 且 `i->cls=Kl` 返回 `Ke=-2`（来自
`ir_ops.h` 的 `T(l,e,e,e, m,e,e,e)`），而 `Ke == Ka == -2`，于是
`fixarg` 走错路径，最终 `rname()` 在 L126 崩溃。

## 修复方案（已应用）

### 方案 A — omap cls 改为 Ki（src/target/aarch64/aarch64_emit.c）
```c
{ Ostoreb, Ki, "strb %W0, %M1" },
{ Ostoreh, Ki, "strh %W0, %M1" },
{ Ostorew, Ki, "str %W0, %M1" },
{ Ostorel, Ki, "str %L0, %M1" },
{ Ostores, Ka, "str %S0, %M1" },   /* 浮点仍按 Ks/Kd，omap[Ka] 兜底 */
{ Ostored, Ka, "str %D0, %M1" },
```

### 方案 B — isel store 特殊处理（src/target/aarch64/aarch64_isel.c）
仿照 `x86_64_isel.c sel()` 在 store 分支显式指定 arg class：
```c
if (isstore(i.op)) {
    emiti(i);
    iarg = curi->arg;
    fixarg(&iarg[0], i.cls, 0, fn);   /* 源类：i.cls（与 emiti 同步） */
    fixarg(&iarg[1], Kl, 0, fn);      /* 地址永远 Kl */
    return;
}
```

## 验证

- `make -C mcc check-targets` 全绿（x86_64/i386/aarch64/loongarch64/riscv64）。
- `make ARCH=aarch64` 全量编译 meuos-libc 通过；交叉 `bare_tls.c` 验证
  `_Thread_local int = 5` 在主线程读到 5（修复前读到 0）。
- `MEUOS_AARCH64_RUN=1 make check-aarch64-bootstrap` qemu-aarch64-static
  端到端：hello 输出 "aarch64 MeuOS libc"，phase2 输出 "counter = 2000"，
  bare_tls 输出 "tls main=5 child=9 errno=31/47"。

## 影响范围
- `src/target/aarch64/aarch64_emit.c` L65-70（omap 存储条目）。
- `src/target/aarch64/aarch64_isel.c` L200-233（`sel()` 函数）。
