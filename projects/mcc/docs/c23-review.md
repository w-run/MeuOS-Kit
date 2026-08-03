# C23 特性覆盖审查

> 分支：`worktree-mxx-work`。日期：2026-08-02。
> 目的：核对 mcc 对 C23 标准特性的覆盖完整性（goal「C 覆盖 90→23」）。
> 方法：test/c23/ 现有测试 + 源码 grep + 临时编译/运行验证。只调研不改码。
> **复核（worker-doc4，HEAD 2c474d4，2026-08-03）**：原 3 个差距已由 **226d31e** 闭环
> （__VA_OPT__/__has_c_attribute/constexpr 函数求值，测试 test/c23/{va_opt,va_opt_boundary,
> has_c_attribute,constexpr_func}.c），下表差距清单已同步。

## 结论摘要

mcc 的 C23 覆盖**高度完整**：`make check-c23` 21 个测试全 PASS，绝大多数 C23
语言特性已实现并有测试。原确认的 **3 个差距**（constexpr 函数编译期求值、
`__VA_OPT__`、`__has_c_attribute`）已由 **226d31e 全部闭环**（2026-08-03，HEAD 2c474d4），
新增 4 个差距测试全过；当前 **C23 无已知差距**。⚠️ 门禁提示：check-chibicc 仍为失败门禁
（早期 41 COMPILEFAIL 的 sysroot 链接根因已由 6ca4ba1 修复，现不再是 0 通过；worker-chi4 调查，排除出 verify-all.sh），见文末「门禁状态」。

## 已支持特性（含证据）

| C23 特性 | 状态 | 证据 |
|:---------|:-----|:-----|
| typeof / typeof_unqual | ✅ | src/c/parse/specs.c:576-586（TTYPEOF/TTYPEOF_UNQUAL）；实测 `typeof(x)`/`typeof_unqual(const int)` 编译+运行 |
| constexpr 变量 | ✅ | src/c/parse/specs.c:68（QUALCONSTEXPR）；test/c23/constexpr.c |
| nullptr / nullptr_t | ✅ | include/mcc.h:60（TYPENULLPTR）；test/c23/nullptr.c / nullptr_full.c / nullptr_t.c |
| #embed（limit/prefix/suffix/if_empty） | ✅ | src/c/lex/pp.c:1031-1049；test/c23/embed*.c（4 个） |
| 二进制字面量 0b/0B | ✅ | src/c/lex/pp_expr.c:43；src/c/lex/pp.c:1333；test/c23/bin_literal.c |
| digit separator `'` | ✅ | src/c/lex/pp.c:1333；test/c23/bin_literal.c |
| u8 字符/字符串字面量 | ✅ | src/c/lex/scan.c:448-452（u 前缀 + '8'）；test/c23 覆盖 |
| 匿名 struct/union 成员 | ✅ | 实测嵌套匿名成员编译+运行（struct_decl.c 位域/匿名处理） |
| `else` 后属性（`else [[attr]]`） | ✅ | 实测 `else [[unlikely]]` 编译通过 |
| auto 类型推导（C23 语义） | ✅ | src/c/parse/specs.c:39,522（TAUTO 作为类型推导指示符） |
| _BitInt(N) 精确定宽整数 | ✅ | test/c23/bitint_full.c（8/31/40/64/33 位全面测试） |
| _Decimal32/64/128（类型系统） | ⚠️ 部分 | test/c23/decimal.c（类型声明）；运算/字面量后缀有限 |
| #elifdef / #elifndef | ✅ | test/c23/elifdef.c |
| __has_include | ✅ | test/c23/has_include.c |
| 空初始化 `{}` | ✅ | test/c23/empty_init.c |
| 标准属性 `[[...]]`（nodiscard/deprecated/unsequenced/reproducible 等） | ✅ | test/c23/attributes.c / attributes_full.c；实测 `[[unsequenced]]`/`[[reproducible]]` |
| labeled break/continue | ✅ | test/c23/labeled_break.c；src/c/parse/stmt.c:10-22 |
| 标签后可声明（C23 放宽） | ✅ | 实测 `goto lab; int j; lab:` 编译通过 |
| #warning | ✅ | test/c23/warning_directive.c |
| 复合字面量 + _Generic（基线） | ✅ | 实测编译+运行 |
| asm 语句 + 属性 | ✅ | test/c23/asm_pragma.c |

## 差距清单（3 项 → 全部已闭环）

| # | 特性 | 原现象 | 状态（HEAD 2c474d4） | 证据 |
|:---:|:-----|:-----|:-----|:-----|
| 1 | **constexpr 函数编译期求值** | `constexpr int s = square(6)` 报「requires a constant expression initializer」；运行时调用 OK | ✅ 已闭环（**226d31e**） | constexpr 函数调用在编译期折叠（整型常量实参委托 cpp_constexpr_eval）；test/c23/constexpr_func.c |
| 2 | **__VA_OPT__** | `#define F(fmt, ...) printf(fmt __VA_OPT__(,) __VA_ARGS__)` 报语法错误 | ✅ 已闭环（**226d31e**） | 变参为空展开为空、否则展开为 tokens；test/c23/va_opt.c + va_opt_boundary.c |
| 3 | **__has_c_attribute** | 无实现（grep 无结果） | ✅ 已闭环（**226d31e**） | 已知标准属性名返回 1（pp_expr 条件表达式宏，与 __has_include/__has_embed 同机制）；test/c23/has_c_attribute.c |

> 结论：**C23 三差距全部闭环，当前无已知差距**。验证：226d31e 隔离干净构建
> check-c23（28 测试）/check-c99/check-c11 全 PASS。

## 验证方法

```sh
MEUOS_SYSROOT=$(pwd)/../sysroot make check-c23   # 21 个测试全 PASS
```

## 门禁状态（HEAD 2c474d4）

| 门禁 | 状态 | 说明 |
|:-----|:-----|:-----|
| check-c23 | ✅ 全 PASS | 28 个测试（含 226d31e 新增 va_opt/va_opt_boundary/has_c_attribute/constexpr_func 4 个差距测试） |
| check-chibicc | **RED 门禁（未通过，排除出 verify-all.sh）** | chibicc 社区套件兼容性仍为失败门禁。早期 41 项 COMPILEFAIL 的根因是 sysroot 路径推导错误（`6ca4ba1` 已修复「cannot find -lc-meuos」链接失败），修复后套件已不再是 0 通过，但仍有大量标准/C conformance 缺陷（如 100f 后缀、UINT64_MAX 字面量类型、宏重定义、_Atomic→void* 转换、__LINE__ 偏差等，见 test/community/chibicc/REPORT.md §5 分类）未闭环。**worker-chi4 调查中**；`test/community/chibicc/results.log` 为 gitignore 易变产物（取决于构建状态，含在途未提交改动时数字会漂移），最新一次运行约 `PASS=9 / RUNFAIL=6 / COMPILEFAIL=26`，门禁整体仍判定失败并排除出 verify-all.sh。 |
| check-pic-verify | FAIL（已知缺口） | riscv64 不发射 GOT 序列（`%got_pcrel_hi` 缺失）、i386 缺 `@GOT(` 序列；x86_64/aarch64 OK。未纳入 verify-all.sh（脚本内标注 known gap）。 |
| verify-all.sh | 17/17 PASS | HEAD 2c474d4 全量门禁通过；上述两门禁因已知缺口被显式排除。 |
