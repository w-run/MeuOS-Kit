# 待修复：mcc aarch64 后端存储到全局变量崩溃

## 背景
编译 aarch64 libc runtime 的 `src/arch/aarch64/tls.c` 时，mcc 在
`src/target/aarch64/aarch64_emit.c` 触发 `dying: invalid class` 断言，
阻塞 aarch64 完整 runtime 移植。

## 复现
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
当前实现（错误）：
```c
/* src/target/aarch64/aarch64_emit.c L65-70 */
{ Ostoreb, Kw, "strb %W0, %M1" },
{ Ostoreh, Kw, "strh %W0, %M1" },
{ Ostorew, Kw, "str %W0, %M1" },
{ Ostorel, Kw, "str %L0, %M1" },     /* BUG: cls=Kw 但 i->cls=Kl */
{ Ostores, Kw, "str %S0, %M1" },
{ Ostored, Kw, "str %D0, %M1" },
```

omap 匹配规则（参考 `emit.c` 的 `emitins`）：
```c
omap[o].cls == i->cls || omap[o].cls == Ka
  || (omap[o].cls == Ki && KBASE(i->cls) == 0)
```
当 `i->cls == Kl`（存储 64 位值）时：
- `Kw != Kl`、`Kw != Ka`、`Kw != Ki` → 匹配失败 → fallback 到 `die`

对比其他后端（正确）：
- `x86_64_emit.c`: `{ Ostorel, Ka, "movq %L0, %M1" }`（Ka 匹配所有类）
- `loongarch64_emit.c`: `{ Ostorel, Ki, "st.d %0, %M1" }`（Ki 匹配 KBASE==0）

### 缺陷 2：`aarch64_isel.c` 缺少 store 特殊处理
当前 `sel()` 对 store 走通用分支（L227-232）：
```c
if (i.op != Onop) {
    emiti(i);
    iarg = curi->arg;
    fixarg(&iarg[0], argcls(&i, 0), 0, fn);   /* argcls 返回 Ke=-2 */
    fixarg(&iarg[1], argcls(&i, 1), 0, fn);
}
```

`argcls(&i, 0)` 对 `Ostorel` 且 `i->cls=Kl` 返回 `Ke=-2`
（来自 `ir_ops.h` 的 `T(l,e,e,e, m,e,e,e)` 宏，只有 `Kw` 列定义）。
而 `Ke == Ka == -2`，于是 `fixarg` 错误地走上浮点常量加载路径
（`KBASE(k) == 1`），最终 `rname()` 在 L126 崩溃。

对比 `x86_64_isel.c` L407-420 有显式 store 处理：
```c
case Ostored:
case Ostores:
case Ostorel:
case Ostorew:
case Ostoreh:
case Ostoreb:
    if (rtype(i.arg[0]) == RCon) {
        if (i.op == Ostored) i.op = Ostorel;
        if (i.op == Ostores) i.op = Ostorew;
    }
    seladdr(&i.arg[1], tn, fn);    /* 用 seladdr 而非通用 fixarg */
    goto Emit;
```

### IR 转储佐证
`mcc --target=aarch64 -dI` 输出：
```
function $scan() {
@start
        %(null) =l copy R1
        %isel.2 =l copy $".Lfp0"
        %isel.1 =s load %isel.2     /* 错误：cls=Ks（浮点），应处理地址 */
        storel %(null), %isel.1
        ret0
}
```

## 修复方案（择一或合并）

### 方案 A（最小补丁，仅改 omap）
将 `aarch64_emit.c` L65-70 的存储条目 cls 改为 `Ki`（匹配 loongarch64）：
```c
{ Ostoreb, Ki, "strb %W0, %M1" },
{ Ostoreh, Ki, "strh %W0, %M1" },
{ Ostorew, Ki, "str %W0, %M1" },
{ Ostorel, Ki, "str %L0, %M1" },
{ Ostores, Ks, "str %S0, %M1" },   /* 浮点仍按 Ks/Kd */
{ Ostored, Kd, "str %D0, %M1" },
```
**注意**：仅靠 omap 修复可能不足，因为 isel 的 `argcls()` 仍返回 `Ke`，
`fixarg` 仍会走错路径。需配合方案 B 或修改 `ir_ops.h` 的 T 宏。

### 方案 B（改 isel，加 store 特殊处理）
仿照 x86_64_isel.c 在 `aarch64_isel.c` 的 `sel()` 增加 store 分支：
```c
case Ostored:
case Ostores:
case Ostorel:
case Ostorew:
case Ostoreh:
case Ostoreb:
    if (rtype(i.arg[0]) == RCon) {
        if (i.op == Ostored) i.op = Ostorel;
        if (i.op == Ostores) i.op = Ostorew;
    }
    /* aarch64 的 seladdr 等价物（fixarg 第二参数 RMem 处理） */
    goto Emit;
```
但 aarch64 isel 当前没有 `seladdr`，地址折叠走 `fixarg` 的 `RMem` 分支
（L124-131），可能要补一条对 `i.arg[1]` 用 `Kl` 而非 `argcls` 的调用。

### 方案 C（改 optab T 宏，定义所有类列）
修改 `include/ir_ops.h` 中 store 条目，将 `T(l,e,e,e, m,e,e,e)` 改为
完整列定义。但 `Ke`/`Km` 是历史遗留（来自 qbe 文本 IL 解析期），
修改 T 宏可能影响其他后端，**不推荐**。

## 推荐路径
1. 先应用方案 A（omap cls → Ki/Ks/Kd），跑现有 `make check-targets`
   确保不退化。
2. 若仍崩，叠加方案 B（isel store 分支）。
3. 用 `tls.c` 复现用例作为回归测试，加入 `test/aarch64/`。

## 影响范围
- `src/target/aarch64/aarch64_emit.c` L65-70（omap 存储条目）
- `src/target/aarch64/aarch64_isel.c` L200-233（`sel()` 函数）
- 可能需要新增 `test/aarch64/store_global.c` 回归用例

## 验收
- `tls.c` 能用 `mcc --target=aarch64 -c` 编译通过
- `make -C projects/mcc check-targets` 全绿（x86_64/i386/aarch64/loongarch64/riscv64）
- `make -C projects/mcc check-c11` 全绿
- 新增的 store_global 回归用例通过
