# C23 特性覆盖审查

> 分支：`worktree-mxx-work`。日期：2026-08-02。
> 目的：核对 mcc 对 C23 标准特性的覆盖完整性（goal「C 覆盖 90→23」）。
> 方法：test/c23/ 现有测试 + 源码 grep + 临时编译/运行验证。只调研不改码。

## 结论摘要

mcc 的 C23 覆盖**高度完整**：`make check-c23` 21 个测试全 PASS，绝大多数 C23
语言特性已实现并有测试。确认 **3 个差距**：constexpr 函数编译期求值、
`__VA_OPT__`、`__has_c_attribute`。

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

## 差距清单（3 项）

| # | 特性 | 现象 | 影响 | 工作量 |
|:---:|:-----|:-----|:-----|:-----:|
| 1 | **constexpr 函数编译期求值** | `constexpr int s = square(6)` 报「requires a constant expression initializer」；运行时调用 OK | 编译期常量计算（C23 §6.7.1/6.6）；影响用 constexpr 函数做常量折叠的代码 | 中（sema/eval 需支持常量上下文函数求值） |
| 2 | **__VA_OPT__** | `#define F(fmt, ...) printf(fmt __VA_OPT__(,) __VA_ARGS__)` 报语法错误 | 变参宏条件展开（C23 §6.10.5）；影响标准库/变参宏 | 小（pp.c 宏展开） |
| 3 | **__has_c_attribute** | 无实现（grep 无结果） | 属性存在性条件编译（C23 §6.10.1）；影响属性兼容代码 | 小 |

## 推荐补全顺序

1. **__VA_OPT__（优先，小成本）**：真实代码（尤其 `printf` 类变参宏、日志宏）常用，
   是 GNU `,##__VA_ARGS__` 的标准化替代。pp.c 的宏展开处加 `__VA_OPT__(tokens)`
   支持——当变参为空时展开为空，否则展开为 tokens。
2. **__has_c_attribute（小成本）**：与 `__has_include`/`__has_embed` 同机制
   （pp_expr 的条件表达式宏），仿现有实现加 `__has_c_attribute(name)`。
3. **constexpr 函数编译期求值（中等，可按需）**：影响面小（编译期常量场景），
   且与 C++ constexpr 机制可复用。建议在 sema/eval 的常量求值器加函数调用折叠。

## 验证方法

```sh
MEUOS_SYSROOT=$(pwd)/../sysroot make check-c23   # 21 个测试全 PASS
```
