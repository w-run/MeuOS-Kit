# C11 + C23 特性补全计划

> 日期：2026-07-23
> 目标：在 libmcc 架构调整和 m++ 工作之前，先统一完成 C11 全特性 + C23 稳定实现的补全。
>
> 原则：先标准化可用，再增强优化。C11 完成度（当前 12/12 测试 PASS，但有 3 个"not yet supported"缺项）
> 后，再推进 C23 特性。

---

## 当前状态快照

### C11 ✅ 已实现（12/12 测试 PASS）

| 特性 | 状态 | 测试文件 |
|------|:----:|---------|
| `_Atomic`（限定符形式） | ✅ | `atomic_basic.c`, `atomic_concurrent.c` |
| `_Generic` | ✅ | `generic.c` |
| `_Thread_local` | ✅ LE/IE | `thread_local.c` |
| `_Alignas` / `alignas` | ✅ | `alignas.c` |
| `_Alignof` / `alignof` | ✅ | `alignof.c` |
| `_Noreturn` | ✅ | `noreturn.c` |
| `_Static_assert` / `static_assert` | ✅ | `static_assert.c` |
| 匿名结构体/联合体 | ✅ | `anon_struct.c` |
| 复合字面量 | ✅ | `compound_lit.c` |
| 指定初始化器 | ✅ | `desig_init.c` |
| VLA | ✅ | `vla.c` |
| varargs | ✅ | `varargs.c` |

### C11 ❌ 缺项

| 特性 | 文件:行 | 当前行为 | 影响 |
|------|---------|---------|:----:|
| `_Atomic(type-name)` 语法 | `specs.c:361-362` | `error("not yet supported")` | **阻塞**：C11 标准要求 |
| `_Complex` | `specs.c:358-359` | `error("not yet supported")` | **重要**：C11 复数运算 |
| `_Imaginary` | token 存在 | 无实现 | **次要**：复数配对类型 |
| `_Decimal32`/`64`/`128` | tokens 111-113 | 无实现 | **次要**：托管实现可选 |
| `typesame()` | `type.c:192-196` | stub → `typecompatible()` | **类型系统** |
| `typecomposite()` | `type.c:198-204` | stub → return t1 | **类型系统** |
| `#pragma` | token 存在 | 无处理 | **次要** |
| 内联汇编 `__asm__` | `stmt.c:305-306` | error | **次要**（可延迟） |
| TLS GD/LD 模型 | 被 P6 阻塞 | 仅 LE/IE | **延迟**（见 .todo/gd-tls.md） |

### C23 ✅ 已部分实现

| 特性 | 状态 | 说明 |
|------|:----:|------|
| `typeof` / `typeof_unqual` | ✅ | 完整实现 |
| `nullptr` | ⚠️ 局部 | 表达式级别（赋值/比较），缺完整 `nullptr_t` 类型语义 |
| `_BitInt(N)` | ✅ | 1-64 位 |
| 属性语法 `[[]]` | ✅ | 含 GNU `__attribute__` |
| `static_assert`（关键字） | ✅ | C11 别名字段 |
| `thread_local`（关键字） | ✅ | C11 别名字段 |
| `constexpr` | ❌ | 仅 token 存在，无实际解析/实现 |

### C23 ❌ 缺项

| 特性 | 当前状态 | 影响 |
|------|---------|:----:|
| `#embed` | 完全未实现 | **重要** |
| `constexpr` 变量/函数 | 仅 token | **重要** |
| `nullptr_t` 全语义 | 缺类型等价规则 | **中等** |
| `#elifdef` / `#elifndef` | 未实现 | **中等** |
| `#warning` | token 存在，未实现 | **中等** |
| 二进制字面量 `0b` | 无 | **中等** |
| 数字分隔符 `'` | 无 | **中等** |
| 空初始化器 `{}` | 未解析 | **中等** |
| `auto` 类型推导 | 未实现 | **中等** |
| `[[fallthrough]]` 等属性 | 未实现 | **中等** |
| 诊断系统 `warn()` | 基础设施就位，`warn()` 未实现 | **中等** |

---

## 批次 A：独立基础设施（可立即 4 路并行）

这 4 个任务修改独立文件，无交叉依赖。

### 任务 1 | `_Atomic(type-name)` 语法支持

**文件**：`src/parse/specs.c`（行 361-362）、`src/sema/type.c`

**当前问题**：`specs.c` 中 `T_ATOMIC` case（行 361）直接报错 `"_Atomic is not yet supported"`。实际上限定符形式已经支持，只有 `_Atomic(type-name)` 作为类型说明符的形式未实现。

**修改方案**：

在 `specs.c` 的 `T_ATOMIC` case 中，不要直接报错。解析接下来的 token：

```c
case T_ATOMIC:
    next();
    if (tok.kind == TLPAREN) {
        /* _Atomic(type-name) 形式 */
        /* 1. 确认不是复合字面量（看 token 流：typename 后能不能匹配 ） */
        /* 2. 解析类型名，构造带 QUALATOMIC 的类型包装 */
        /* 3. 设置 *t, *tq */
    } else {
        /* _Atomic 限定符形式（已有，按原有的走到 ts 位掩码路径） */
        /* 但这里可能不对，需要看逻辑 */
    }
```

**详细设计**：

1. 当前 `T_ATOMIC` 在 `specs.c` 的 switch 中是被当作类型说明符处理的。方式一：`_Atomic` 作为类型限定符（用 `next()` 移到 `ts` 位掩码路径），方式二为 `_Atomic(type-name)` 作为类型说明符。

2. 实际上在 C11 标准中，`_Atomic` 可以出现在两个位置：
   - 作为**类型限定符**（与 `const`/`volatile`/`restrict` 一起）：`_Atomic int x;`
   - 作为**类型说明符**（产生复合原子类型）：`_Atomic(int) x;`

3. 当前代码中 `_Atomic` 进入的是类型说明符路径，但实际只作为限定符处理了。需要：
   - 当 `T_ATOMIC` 紧跟 `(type-name)` 时是类型说明符
   - 当 `T_ATOMIC` 后面直接是类型名或限定符时是限定符

4. 参考 [`reference/cproc/src/parse/specs.c`] 的处理方式。

**验收**：
- `_Atomic(int) x = 0;` 编译通过
- `_Atomic(struct S) y;` 编译通过  
- `_Atomic int z;` 仍然可用（限定符形式）
- 回归：`make check-c11` 全绿

---

### 任务 2 | `typesame()` / `typecomposite()` 真正实现

**文件**：`src/sema/type.c`（行 192-204）

**当前问题**：两个函数都是 stub：
- `typesame()` 回退到 `typecompatible()`——这意味着 C11 要求严格名称等价的地方（如 `_Generic` 关联列表匹配）行为不正确。
- `typecomposite()` 直接返回 `t1`

**实现参考**：[`reference/cproc/src/sema/type.c`]

**`typesame()` 语义**（C11 §6.2.7）：
- 两个类型**完全相同**当且仅当它们所有组成都相同
- 对数组：长度必须相等
- 对函数：参数类型列表必须一一相同
- 对指针：base 类型必须相同
- 对限定：限定词必须相同

**`typecomposite()` 语义**（C11 §6.2.7）：
- 两个兼容类型合并为一个"复合类型"
- 数组：长度取已知值，限定词取合并（交集）
- 函数：参数类型用 `typecomposite` 合并，`...` 保持，`void` 列表与完整列表合并

**实现步骤**：

```c
bool
typesame(struct type *t1, struct type *t2)
{
    if (t1 == t2) return true;
    if (t1->kind != t2->kind) return false;
    switch (t1->kind) {
    case TYPECHAR: case TYPEINT: case TYPELONG: ...  /* 标准整数类型 */
        return t1->u.arith.issigned == t2->u.arith.issigned
            && t1->u.arith.width == t2->u.arith.width;
    case TYPEPOINTER:
        return t1->qual == t2->qual && typesame(t1->base, t2->base);
    case TYPEARRAY:
        if (t1->incomplete != t2->incomplete) return false;
        if (!t1->incomplete && t1->size != t2->size) return false;
        return typesame(t1->base, t2->base)
            && t1->qual == t2->qual;
    case TYPEFUNC:
        // 检查返回值 + 参数列表
        ...
    case TYPESTRUCT: case TYPEUNION:
        return t1 == t2;  /* struct 标签唯一性 */
    default:
        return false;
    }
}
```

**验收**：
- `make check-c11` 全绿
- `_Generic` 选择更精确（之前 `typesame` 和 `typecompatible` 无区别，现在 `typesame` 更严格）
- 新增测试：`test/c11/type_same.c`

---

### 任务 8 | `#elifdef` / `#elifndef` 预处理指令

**文件**：`src/lex/pp.c`、`include/tokens.h`

**C23 新增**：`#elifdef identifier` 等价于 `#elif defined(identifier)`，`#elifndef` 等价于 `#elif !defined(identifier)`。

**实现**：
1. `tokens.h` 中新增 `TELIFDEF` / `TELIFNDEF` token（确认是否已存在，当前只有 `TELIF`）
2. `pp.c` 的条件编译栈处理中，在 `#elif` 分支旁新增 `#elifdef` / `#elifndef` 的解析：
   ```
   用 token 类型区分 → 调用 handle_elifdef(defined_ident) / handle_elifndef(!defined_ident)
   ```

**token 定义**：
```c
TOKEN(TELIFDEF,       "elifdef")
TOKEN(TELIFNDEF,      "elifndef")
```

**验收**：`test/c23/elifdef.c`：
```c
#define X
#ifdef X
   int a = 1;
#elifdef Y
   int a = 2;
#else
   int a = 3;
#endif
```

---

### 任务 10 | 二进制字面量 `0b` + 数字分隔符 `'`

**文件**：`src/lex/scan.c`

**实现**：
1. **二进制字面量**：在 `scan.c` 的数字扫描函数中，识别 `0b` / `0B` 前缀：
   ```c
   if (c == '0' && (peek() == 'b' || peek() == 'B')) {
       next(); /* 跳过 b/B */
       while (c == '0' || c == '1' || c == '\'') { /* 每隔数字跳过了 */
           if (c != '\'') val = (val << 1) | (c - '0');
           next();
       }
       return mknumber(val, 2);
   }
   ```

2. **数字分隔符**：在所有数字扫描循环中（十进制、十六进制 `0x`、八进制 `0o`、二进制 `0b`），添加 `if (c == '\'') continue;` 跳过分隔符。C23 要求数字分隔符可出现在任何数字之间，但不能在开头/结尾、不能连续、不能影响前缀中的 `x`/`b`。

**验收**：`test/c23/bin_literal.c`：
```c
int a = 0b1101'0010;  /* 二进制 0xD2 */
long b = 1'000'000;   /* 1000000 */
int c = 0xFF'00;       /* 十六进制 0xFF00 */
```

---

## 批次 B：独立实现（可与 A 并行）

### 任务 9 | `#warning` 预处理指令

**文件**：`src/lex/pp.c`（`TWARNING` 在 `tokens.h:133` 已存在）

**实现**：在 pp 主循环的 directive 分发中添加 `#warning`：
```c
case TWARNING:
    skip_to_newline();  /* 收集所有 token 到换行为警告消息文本 */
    fprintf(stderr, "%s:%d: warning: %s\n",
            curfile->name, curfile->line, msg);
    break;
```

**验收**：`test/c23/warning_directive.c`（检查 stderr 输出）

---

### 任务 11 | 空初始化器 `{}`

**文件**：`src/parse/decl.c`、`src/sema/init.c`

**C23 允许**：空初始化列表 `T x = {};` 对任意类型产生零初始化。

**当前行为**：解析器期望初始化列表至少有一个元素。需要将空 `{}` 视为特例。

**实现**：
- `decl.c` 的 `declcommon()` 或 `initializer()` 函数中，检测 `{ }`（TLBRACE 后立即 TRBRACE）
- 调用 `zeroinit(type)` 生成等效于 `{0}` 的初始化器
- `zeroinit(type)` 递归生成零初始化：标量→0，指针→NULL，数组→递归每个元素，结构体→递归每个成员

**验收**：`test/c23/empty_init.c`：
```c
int x = {};        /* x = 0 */
struct S { int a; int b; } s = {};  /* s.a = 0, s.b = 0 */
int arr[10] = {};  /* 所有元素为 0 */
```

---

### 任务 13 | 诊断系统 `warn()` 函数

**文件**：新文件 `src/diag/warn.c`、`include/mcc.h`（声明）、`src/driver/main.c`（-W 选项串联）

**背景**：`-O`/`-W` 基础设施已在 `opt-warn-levels.md` 中完成：
- `Fn` 已有 `optlevel`/`warnlevel` 字段
- 已有 `WARN_*` bitmask 宏（`WARN_UNUSED`, `WARN_TYPE`, `WARN_IMPLICIT`, `WARN_RETURN`, `WARN_ALL`）
- `-w`/`-Wall`/`-Werror` 解析已就绪

**实现**：
1. 新增 `src/diag/warn.c`：
```c
void warn(struct location *loc, int kind, const char *fmt, ...) {
    if (!(warn_level & kind)) return;
    fprintf(stderr, "%s:%d:%d: warning: ", loc->file, loc->line, loc->col);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    if (warn_as_error) errors++;
}
```
2. 在 `include/mcc.h` 中声明 `warn()`，`extern int warn_level; extern bool warn_as_error;`
3. 在 `src/driver/main.c` 中将全局 `warn_level`/`warn_as_error` 设置到编译单元

**验收**：
- `mcc -w test.c` 抑制警告
- `mcc -Wall -c test.c` 输出警告
- `mcc -Werror -c test.c` 将警告升级为错误

---

## 批次 C：复杂实现（串行为主）

### 任务 3 | `_Complex` / `_Imaginary` 复数类型

**文件**：`include/mcc.h`、`src/parse/specs.c`、`src/sema/type.c`、`src/sema/eval.c`、`src/irgen/expr.c`

**实现策略**：

1. **类型系统**：`mcc.h` 中确认 `TYPECOMPLEX` typekind 决定是否添加（当前 `enum typekind` 没有独立复数类型）。方案是以 `TYPEDOUBLE`/`TYPEFLOAT`/`TYPELDOUBLE` 为基础，配合 `arith.iscomplex` 标记。

2. **语法解析**：`specs.c` 的 `T_COMPLEX` case 从 error 改为：
```c
case T_COMPLEX:
    ts |= SPECCOMPLEX;
    ++ntypes;
    next();
    /* SPECCOMPLEX 必须跟在浮点类型后：_Complex double, _Complex float, _Complex long double */
    break;
```
然后在类型构造时检查 `SPECCOMPLEX` 标记，将基础浮点类型包装成复数类型。

3. **IR 降级**：复数运算在 IR 层表示为两个独立标量的操作：
   - 复数 = `{real_part, imag_part}` 的 struct 等价
   - 加法：`(r1+r2, i1+i2)`
   - 乘法：`(r1*r2 - i1*i2, r1*i2 + r2*i1)`
   - 比较：先检查 real，再检查 imag
   - 初值：`__real__` 和 `__imag__` 内置操作数

4. **`__real__` / `__imag__` 扩展**：GNU C 扩展，通过内建函数访问复数实部/虚部

**验收**：`test/c11/complex.c`：
```c
_Complex double z = 1.0 + 2.0 * I;
z = z * 2.0;
double r = __real__ z;
double i = __imag__ z;
```

---

### 任务 5 | `#embed` 预处理器指令

**文件**：`include/tokens.h`、`src/lex/pp.c`、`src/sema/eval.c`

**实现方案**：

1. **Token 定义**：新增 `TEMBED`

2. **pp.c 解析**：
```c
case TEMBED:
    next();  /* 跳过 #embed */
    path = parse_include_path();  /* 重用 #include 的路径解析 */
    /* 可选参数: limit(N), prefix(N), suffix(N), if_empty(N) */
    while (tok.kind == TLPAREN) {
        parse_embed_params();
    }
    data = read_file_bytes(path);
    /* 将 data 转换为逗号分隔的整数常量，返回到 token 流 */
    unget_token_list(convert_to_comma_list(data, len));
    break;
```

3. **关键设计**：`#embed` 将文件内容转换为一系列 `unsigned char` 常量值，以逗号分隔插入 token 流。这对于静态初始化器支持很关键。

4. **limit() 参数处理**：限制读取的最大字节数。

**验收**：`test/c23/embed.c`：
```c
static const unsigned char icon[] = {
    #embed "test.bin"
};
```

---

### 任务 6 | `constexpr` 变量/函数

**文件**：`src/parse/specs.c`、`src/parse/declarator.c`、`src/sema/eval.c`

**C23 `constexpr`**：
- `constexpr int N = 42;`——编译时常量，可用于数组长度、case 标签、位域宽度
- `constexpr int fn(int x) { return x * 2; }`——常量表达式函数，可在编译时求值

**实现**：

1. **变量 constexpr**：
   - 在 `specs.c` 将 `TCONSTEXPR` 作为类型说明符解析（类似 `const`/`volatile` 组合）
   - `constexpr` 隐含 `const` 语义
   - 在 `eval.c` 中确保 `constexpr` 变量的值可以常量求值（必须是 ICE）
   - 在 `decl.c` 中如果 `constexpr` 变量没有初始化器，报错

2. **函数 constexpr**（简化版本）：
   - 识别 `constexpr` 函数声明
   - 检查函数体是否为单个 `return constant_expression;`
   - 在编译时求出该值存入符号表
   - 后续使用处替换为该常量值

3. **`constexpr` 作为新 bool 字段**：
```c
struct decl {
    ...
    bool is_constexpr;  /* C23 constexpr */
    ...
};
```

**验收**：`test/c23/constexpr.c`：
```c
constexpr int N = 42;
int arr[N];  /* VLA 或等价的定长数组 */
constexpr int square(int x) { return x * x; }
int y = square(5);  /* 编译时求值为 25 */
```

---

### 任务 7 | `nullptr_t` 全语义

**文件**：`src/sema/type.c`、`src/sema/eval.c`、`src/irgen/expr.c`、`src/parse/expr.c`

**当前 `nullptr` 实现**：
- `TNULLPTR` token，`typenullptr` 类型
- 表达式级别：`nullptr` 可作为表达式求值
- 赋值规则：`nullptr` 可赋给指针类型

**缺失的 `nullptr_t` 全语义**：

1. **`nullptr_t` 类型规则**：
   ```c
   typeof(nullptr)  /* → nullptr_t 类型 */
   nullptr_t n = nullptr;  /* 必须成立 */
   ```

2. **隐式转换规则**：
   - `nullptr_t` → 任意指针类型：隐式转换
   - `nullptr_t` → `bool`：得到 `false`
   - `nullptr_t` → 整数类型：**不合法**
   - 指针类型 → `nullptr_t`：不合法

3. **赋值和比较**：
   - `int *p = nullptr;`（隐式转换）
   - `p == nullptr`（比较）
   - `nullptr_t` 变量之间可赋值和比较

**实现**：
- `type.c` 的 `typecompatible()` 中添加 `nullptr_t` 与其他指针类型的兼容规则
- `eval.c` 中添加 `nullptr` 常量求值（特殊标记 `VAL_NULLPTR`）
- `irgen/expr.c` 中 `nullptr` → 0 的 IR 降级

**验收**：`test/c23/nullptr_full.c`：
```c
int *p = nullptr;         /* 有效隐式转换 */
bool b = nullptr;         /* b = false */
decltype(nullptr) n;      /* nullptr_t 类型 */
n = nullptr;              /* 自赋值有效 */
/* int x = nullptr; */    /* 应该编译错误 */
```

---

### 任务 14 | GD-TLS IR 符号类型 + 断言宽松 + 指令序列

**文件**：`include/ir.h`、`src/ir/ir_util.c`、`src/ir/printfn.c`、`src/opt/fold.c`、`src/opt/alias.c`、`src/target/x86_64/x86_64_emit.c`

**详见 `gd-tls.md`，此处总结**：

1. **`SGenThr = 4`**：在 `include/ir.h` 的 `Sym.type` 枚举中新增
2. **断言宽松**：6 个文件中 `con->sym.type & ~SExt) == SGlo` 改为同时兼容 `SGenThr`：
   ```c
   assert((con->sym.type & ~SExt) == SGlo || con->sym.type == SGenThr);
   ```
3. **x86_64 GD 指令序列**：
   ```c
   case SGenThr:
       emit(IMov, RTmp(rdi), ...);  /* leaq sym@tlsgd(%rip), %rdi */
       emit(ICall, ...);            /* call __tls_get_addr@plt */
       break;
   ```

**验收**：end-to-end 验证在 P6 动态链接之前无法完成，但可验证：
- `mcc --target=x86_64 -fPIC -S tls_gd.c` 生成含 `__tls_get_addr` 的汇编
- `make check` 全绿（不破坏 LE/IE）

---

## 批次 D：补充 + 收尾（可并行）

### 任务 4 | `_Decimal32/64/128` 占位

**文件**：`include/mcc.h`（新增 typekind）、`src/parse/specs.c`、`src/sema/type.c`

**最小可用实现**：
```c
/* include/mcc.h */
enum typekind {
    ...
    TYPEDECIMAL32,
    TYPEDECIMAL64,
    TYPEDECIMAL128,
};

/* src/sema/type.c */
struct type *mkdecimaltype(int kind)
{
    struct type *t = mktype(kind, PROPREAL | PROPSCALAR);
    t->size = kind == TYPEDECIMAL32 ? 4 : kind == TYPEDECIMAL64 ? 8 : 16;
    t->align = t->size;
    return t;
}
```

**验收**：`test/c11/decimal.c`：
```c
_Decimal32 d32;   /* 编译通过 */
_Decimal64 d64;   /* 编译通过 */
_Decimal128 d128; /* 编译通过 */
```

---

### 任务 12 | `auto` 类型推导

**文件**：`src/parse/declarator.c`、`src/sema/type.c`

**C23 `auto`**：不再作为存储类说明符，而是类型推导指示符。

**实现**：
1. 在 `specs.c` 中检测 `TAUTO` 作为类型说明符
2. 在 `declarator.c` 的 `declcommon()` 中，当类型为 `auto` 时，在解析完初始化器后回填类型
3. `auto` 与指针/数组/const 组合：
   - `auto x = 42;` → `int x`
   - `const auto x = 42;` → `const int x`
   - `auto *p = &x;` → `int *p`

**验收**：`test/c23/auto.c`

---

### 任务 15 | C23 标准属性

**文件**：`src/parse/attr.c`、`include/decl_internal.h`

**C23 标准属性**：
- `[[fallthrough]]`：标记 switch case 穿透是有意的
- `[[nodiscard]]`：函数返回值不应被忽略
- `[[maybe_unused]]`：抑制未使用警告
- `[[deprecated("msg")]]`：标记废弃声明

**实现**：在 `attr.c` 的 `parse_stdattr()` 中新增 4 个属性名处理：

```c
if (strcmp(name, "fallthrough") == 0) {
    d->u.attr.fallthrough = true;
} else if (strcmp(name, "nodiscard") == 0) {
    d->u.attr.nodiscard = true;
} else if (strcmp(name, "maybe_unused") == 0) {
    d->u.attr.maybe_unused = true;
} else if (strcmp(name, "deprecated") == 0) {
    parse_deprecated_args(); /* 可选消息 */
    d->u.attr.deprecated = true;
}
```

**验收**：`test/c23/attributes.c`

---

### 任务 16 | 测试 + 门禁

**文件**：`projects/mcc/Makefile`、`test/c11/*.c`、`test/c23/*.c`

**具体工作**：
1. 为每个新功能编写对应测试文件（`test/c11/` 或 `test/c23/`）
2. Makefile 新增 `check-c23` 目标（类似 `check-c11`）
3. 运行 `make check` / `make check-c11` / `make check-c23` 全量回归

**测试清单**：
- `test/c11/atomic_typename.c`
- `test/c11/type_same.c`
- `test/c11/complex.c`
- `test/c11/decimal.c`
- `test/c23/elifdef.c`
- `test/c23/bin_literal.c`
- `test/c23/warning_directive.c`
- `test/c23/empty_init.c`
- `test/c23/embed.c`
- `test/c23/constexpr.c`
- `test/c23/nullptr_full.c`
- `test/c23/auto.c`
- `test/c23/attributes.c`

---

## 执行顺序与并行策略

```
批次 A（4 路并行）：
  T1: _Atomic(type-name)    ← specs.c + type.c
  T2: typesame/composite    ← type.c（与 T1 不同函数，无冲突）
  T8: #elifdef/#elifndef    ← pp.c（独立于 T1/T2）
  T10: 0b/' 字面量          ← scan.c（完全独立）

批次 B（3 路并行，可与 A 同步）：
  T9:  #warning             ← pp.c（与 T8 同一文件，需串行或合入）
  T11: {} 空初始化器         ← decl.c + init.c（独立）
  T13: warn() 诊断系统       ← 新文件 diag/warn.c（独立）

批次 C（串行为主）：
  T3:  _Complex              ← 三层：类型+语法+IR（最大任务）
  T5:  #embed                ← pp + sema + irgen
  T6:  constexpr             ← parser + sema
  T7:  nullptr_t             ← type + irgen（依赖 T2 的 typesame）
  T14: GD-TLS 缺口1-2-4      ← IR + emit（部分依赖 T1 类型系统）

批次 D（全并行收尾）：
  T4:  _Decimal*              ← specs.c + type.c
  T12: auto 类型推导           ← declarator.c + type.c
  T15: C23 属性               ← attr.c
  T16: 测试 + 门禁             ← Makefile + test/
```

---

## 验收标准

1. `make check` → `exit=0`（Phase 1a 门禁）
2. `make check-c11` → 16+ 测试全部 PASS（含新增 4 个测试）
3. `make check-c23` → 8+ 测试全部 PASS
4. `mcc -S -std=c23 test.c` 对所有 C11 特性正常编译
5. 回归测试不退化：`make check-targets` / `make check-i386-runtime` / `make check-loongarch64`
6. mcc 自重编译（`make check-sysroot-static mcc`）全绿

---

## 关联文档

- `.todo/gd-tls.md` — GD-TLS 详细设计
- `.todo/opt-warn-levels.md` — `-O` / `-W` 基础设施
- `.todo/warning-system.md` — 诊断系统详细设计
- `../reference/cproc/` — typesame/typecomposite 实现参考
- `../reference/cxx-frontend/` — C++23 前端参考（确保 C23 标准正确）
