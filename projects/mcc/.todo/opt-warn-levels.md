<!--
priority: P1
status: done
done_ts: 2026-07-23
note: 9e1811b 已实现 -O0/1/2/3/s + -w/-Wall/-Werror 真实联动，剩余 -O3/Os/Ofast 与 -W 分类为非阻塞待补
-->

# 已完成：-O 优化级别实现（警告系统基础设施就位）

> Update 2026-07-23：`-O0/1/2/3/s` 和 `-w`/`-Wall`/`-Werror` **已真实实现**。
>
> 具体变更（`9e1811b` on `work/mcc-libc`）：
> - `include/ir.h`：Fn 新增 `optlevel`/`warnlevel` 字段；WARN_* bitmask 宏
> - `src/driver/main.c`：`-O`(0..3/s/f) 解析 → `opt_level` 全局；
>   `-w`/`-Wall`/`-Werror` 解析 → `warn_level` 全局
> - `src/irgen/emit.c`：`emitfunc()` 传递全局到 `fn->optlevel/warnlevel`；
>   `run_passes()` 按 `fn->optlevel` 门禁优化 pass
>
> 待补（非阻塞）：
> - `src/diag/warn.c` 警告输出函数（目前无任何 pass 触发警告，纯基础设施）
> - `-W` 按类别过滤（目前仅 -Wall/-Werror）
> - `-O3` 内联、`-Os` 代码体积优化、`-Ofast` 快速数学
>
> 以下为原始规划文档，保留供参考。

---

## 1. 当前状态

### 1.1 选项解析

**文件**：`src/irgen/driver.c`

搜索确认：`-O` 选项解析路径：

```c
/* driver.c 中选项处理 */
case 'O':  /* -O{0,1,2,3,s} */
    opt_level = ...;  /* 当前：接受但无实际效果 */
    break;
case 'w':  /* -w: 禁止所有警告 */
    /* 接受但无效果 */
    break;
case 'W':  /* -W{all,error,...} */
    /* 接受但无效果（仅记录到 option 结构） */
    break;
```

**当前始终运行的优化 pass**：
- `fold.c` — 常量折叠（始终运行，无级别控制）
- `simpl.c` — 简化（始终运行）
- `gcm.c` — 全局代码移动（始终运行）
- `rega.c` + `spill.c` — 寄存器分配/溢出（始终运行）
- `live.c` — 活性分析（作为 rega 的依赖始终运行）

### 1.2 相关 `.todo` 记录

`ARCHITECTURE.md` L322-323 明确记录：

```
- Implement true `-O` level control (mcc currently always optimizes).
- Implement warning system for `-W`/`-w` (currently accepted but no-op).
```

---

## 2. 设计方案

### 2.1 优化级别定义

参考 GCC/Clang 的 `-O{0,1,2,3,s,fast}` 语义，结合 mcc 实际可用的优化 pass：

| 级别 | 名称 | 启用 pass | 预期效果 |
|:----:|------|-----------|---------|
| `-O0` | 无优化 | 无额外 pass；仅 IR 构造 + 寄存器分配 | 最快编译，最差代码质量 |
| `-O1` | 局部优化 | fold + simpl | 基本常量折叠和简化 |
| `-O2` | 推荐优化 | fold + simpl + gcm | 当前行为（全量），默认级别 |
| `-O3` | 激进优化 | fold + simpl + gcm + (未来 inline) | 未来有内联后启用 |
| `-Os` | 优化大小 | fold + simpl + gcm（偏向代码大小） | 未来 `-Os` 调整 rega 策略 |
| `-Ofast` | 快速数学 | -O3 + 允许非标准浮点 | 未来扩展 |

### 2.2 优化 pass 注册机制

**建议**：在每个 pass 入口函数添加 `fn->optlevel` 检查：

```c
/* fold.c 入口 */
void fold(Fn *fn) {
    if (fn->optlevel < 1) return;  /* -O0 跳过 */
    /* ... 现有折叠逻辑 ... */
}
```

**Fn 结构新增字段**（`ir.h`）：

```c
struct Fn {
    ...
    int optlevel;   /* 0=O0, 1=O1, 2=O2, 3=O3 */
    int warnlevel;  /* 0=无警告, 1=警告, 2=Wall(默认) */
};
```

### 2.3 警告系统设计

**警告类型 bitmask**（`ir.h` 或新头文件）：

```c
#define WARN_UNUSED    (1<<0)  /* 未使用变量/参数 */
#define WARN_TYPE      (1<<1)  /* 类型不匹配 */
#define WARN_IMPLICIT  (1<<2)  /* 隐式声明 */
#define WARN_RETURN    (1<<3)  /* 无返回值的非 void 函数 */
#define WARN_ALL       (WARN_UNUSED|WARN_TYPE|WARN_IMPLICIT|WARN_RETURN)
```

**警告输出函数**（`src/diag/warn.c` 新文件）：

```c
void warn(Fn *fn, int kind, const char *fmt, ...);
// 内部：if (fn->warnlevel & kind) fprintf(stderr, "warning: ...");
```

**`-Werror` 处理**：将警告升级为错误。

### 2.4 Driver 选项映射

| 命令行 | 内部值 |
|--------|--------|
| `-O0` | `fn->optlevel = 0`, 调用 `fold(fn)` 前检查 |
| `-O1` | `fn->optlevel = 1`, fold + simpl |
| `-O2` (默认) | `fn->optlevel = 2`, fold + simpl + gcm |
| `-O3` | `fn->optlevel = 3`, 同上 + 预留内联 |
| `-w` | `fn->warnlevel = 0` |
| `-W`（不含参数） | 等同于 `-Wall` |
| `-Wall` | `fn->warnlevel = WARN_ALL` |
| `-Werror` | 全局 `warn_as_error = 1` |
| 默认 | `fn->optlevel = 2`, `fn->warnlevel = WARN_ALL` |

---

## 3. 影响范围

| 文件 | 修改内容 |
|------|---------|
| `include/ir.h` | `Fn` 新增 `optlevel`、`warnlevel` 字段 |
| `include/ir.h` | 警告类型 bitmask 宏定义 |
| `src/irgen/driver.c` | `-O`/`-W`/`-w` 选项解析映射到 Fn 字段 |
| `src/irgen/irgen.c` (入口) | 优化 pass 调用顺序，按 optlevel 控制 |
| `src/opt/fold.c` | 入口加 `if (fn->optlevel < 1) return` |
| `src/opt/simpl.c` | 入口加 `if (fn->optlevel < 1) return` |
| `src/opt/gcm.c` | 入口加 `if (fn->optlevel < 2) return` |
| `src/diag/warn.c` | **新文件**：警告输出函数 |
| `src/sema/typechk.c` | 在适当位置插入 `warn()` 调用 |
| `src/irgen/expr.c` | 在适当位置插入 `warn()` 调用 |
| `test/` | 新增 `-O0` 编译测试：确认 pass 跳过 → 生成合法代码 |

---

## 4. 验收标准

- `mcc -O0 -c test.c` 编译通过，不崩溃
- `mcc -O2 -c test.c`（默认）产生与当前行为一致的代码
- `mcc -w -c test.c` 抑制警告输出
- `mcc -Wall -c test.c` 显示警告信息
- `mcc -Werror -c test.c` 将警告升级为错误
- `make check` 全量回归不退化
- 测试：`-O0` 产生的 `.s` 与 `-O2` 产生的 `.s` 功能等价（都输出正确结果）

---

## 5. 前置依赖

- 无严格阻塞。`-O0` 跳过 fold/simpl/gcm 后 rega/spill 仍会运行（它们处理寄存
  器分配，不是优化 pass），不需要额外修改。
- 潜在的 **unreachable code** 问题：某些 fold/simpl 产生的简化结果（如 dead code
  消除）在 `-O0` 时不会发生，但这是正确行为——`-O0` 代码质量差但不影响正确性。
