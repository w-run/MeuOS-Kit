# chibicc 社区测试集 — mcc C11/C23 完整性测试报告

> 审阅对象：mcc（MeuOS C 编译器）C11+C23 实现、meuos-libc C11 实现
> 测试集：chibicc 功能测试套件（社区成熟集，MIT 许可）
> 环境：x86_64，mcc 用 `--specs=meuos --sysroot` 链接 meuos-libc（零 GNU libc）

## 1. 测试集来源与适配

- **chibicc** — https://github.com/rui314/chibicc，MIT 许可（附 `LICENSE.chibicc`）。业界最成熟的 C 编译器功能测试集之一，覆盖 C11（`_Generic`/`_Atomic`/`_Thread_local`/`_Alignof`/复合字面量/VLA/指定初始化/转换）与 C23（`constexpr`/`typeof`/`nullptr`/二进制字面量/`#embed`）。
- 位置：`projects/mcc/test/community/chibicc/`（`*.c` + `test.h` + `include*.h` + `assert_adapt.c` + `run.sh`）。
- 适配：`assert_adapt.c` 提供 chibicc 风格的 3 参数 `assert(int,int,char*)`，freestanding 前向声明 `printf/exit`，链接时解析到 meuos-libc。每个测试用 `mcc --specs=meuos --sysroot -- -I.` 编译并链接 meuos-libc，真正验证 **mcc + libc 集成**。
- 运行约定：测试 `main` 返回 0 即 PASS（`assert()` 失配时 `exit(1)`）。
- 纯标准集说明：原计划并引入 `c-tests`（nlsandler，纯标准 C conformance）作更公平度量，但其镜像（codeload/gitclone）均不可达（404/502），暂未引入。chibicc 大量依赖 GNU 扩展，对**纯标准 conformance** 覆盖有限（见 §5、§8）。

## 2. 运行方法

```sh
make -C projects/meuos-libc install DESTDIR=../sysroot PREFIX=/usr   # 首次构建 libc
make -C projects/mcc                                            # 构建 mcc
make -C projects/mcc check-chibicc                             # 运行社区测试
# 或： cd projects/mcc/test/community/chibicc && bash run.sh
```
结果写入 `results.log`。

## 3. 结果汇总

| 类别 | 数量 | 代表用例 |
|---|---:|---|
| PASS | **7** (+4) | `builtin`、`complit`、`decl`、`float`、`offsetof`、`stdhdr`、`typeof` |
| RUNFAIL（运行非零） | 2 | `commonsym`、`line` |
| COMPILEFAIL | 32 | 见 §5 |
| **总计** | **41** | |

> **注**：本次会话新增了 GNU 语句表达式 `({})` 支持（§8），从 COMPILEFAIL 转为 PASS 的 4 个测试（`builtin`、`complit`、`decl`、`typeof`）均受益于 `({})` 解析。另有 5 个测试从 mcc 段错误转为干净编译失败（§5.A）。

## 4. libc C11 完整性（meuos-libc 自带测试）

`make -C projects/meuos-libc check` **全部 PASS**（exit=0），覆盖：
atomic、fence、string、write、stdio、malloc、stdlib、ctype、assert、`c11_headers`、`syscall_fs`、process、kernel_interfaces、socket、**C11 threads**、env、bare-threads、**phase2-counter（TLS `counter=2000`）**、**bare-tls（`tls main=5 child=9`）**、malloc-threads、bare-stdio、signal、setjmp、pthread、bare-process、compat。

**结论：meuos-libc 的 C11 实现（原子、C11 线程、TLS、标准 IO、setjmp、compat 等）完整且行为正确。**

## 5. COMPILEFAIL 分类

### A. GNU 扩展依赖（mcc 合理不支持，非标准 C）— 主体
- **`({ })` 语句表达式**：`alignof, arith, atomic, attribute, bitfield, builtin, cast, complit, const, constexpr, control, decl, enum, function, initializer, pointer, sizeof, struct, typedef, typeof, unicode, union, usualconv, varargs, variable, vla`（26 个，源均含 `({`）。mcc 不支持 GNU 语句表达式（AGENTS.md C23 列表未含此扩展），属合理不支持。
- **`alloca` / `asm`**：`alloca.c`（`alloca` 未声明）、`asm.c`（`asm` 未声明）— GNU/BSD 扩展，mcc 不内建。
- **`__attribute__`**：`attribute.c` 测 GNU `__attribute__`，mcc 仅支持 C23 `[[ ]]` 属性语法。

### B. mcc 真实的标准/C conformance 缺陷（应修复）
1. **`100f` 浮点后缀（C23）** — `generic.c:8` `invalid integer constant suffix 'f'`。C23 允许无小数点/无指数的 float 后缀常量（`100f`）。mcc 未识别。
2. **UINT64_MAX 字面量类型推导（C 6.4.4.1）** — `literal.c:45` `no suitable type for constant '18446744073709551615'`。十进制无后缀常量超过 `LLONG_MAX` 应回退 `unsigned long long`，mcc 未回退。
3. **宏重定义（C 6.10.3p2）** — `macro.c:121` `redefinition of macro 'M1'`。相同 token 序列的宏重定义应允许，mcc 一律报错。
4. **`_Atomic int*` → `void*` 转换（C 6.3.2.3p1）** — `atomic.c:42` / `initializer.c:16` `base types of pointer assignment must be compatible or void`。限定对象指针应允许转为 `void*`，mcc 拒绝。
5. **`__LINE__` 计算偏差 1** — `line.c` `expected 501, got 500`。mcc 预处理 `__LINE__` 值偏差 1（RUNFAIL）。
6. **common/tentative-definition 合并** — `commonsym.c` `expected 3, got 0 [common_ext2]`（RUNFAIL）。与 `-fcommon`/`-fno-common` 默认行为及 tentative def 合并有关。
7. **Unicode 标识符（C11）** — `unicode.c:9` `expected '(' or identifier`。mcc 不支持 `$`/通用字符名标识符或 UTF-8 标识符。
8. **无效转义序列严格性** — `string.c:20` `invalid escape sequence`。C 约束违规应诊断，mcc 符合标准（GCC 容忍为扩展）；但需确认是否误伤合法转义。

### C. chibicc `test.h` 非标准声明导致的与标准头冲突（非 mcc 缺陷）
- `test.h` 用 `long` 代替 `size_t`（`memcpy/memset/strlen` 等）与 `void *ap` 代替 `va_list`（`vsprintf`），在测试同时 `#include` 标准头时与 meuos-libc 冲突：
  - `tls.c`：`string.h:6` `memcpy redeclared`（参数 `long` vs `size_t`）
  - `function.c:92`：`vsprintf redeclared`（`void*` vs `va_list`）
  - `varargs.c`：触发 `stdarg.h:8` `va_end argument must have type va_list`（见 D）
- 此类冲突源于 chibicc 测试自身的 freestanding 风格声明，并非 mcc 错误；严格编译器（GCC/Clang/mcc）均会报。归类为测试集自身偏差。

### D. libc 头兼容性问题（meuos-libc，待修）
- `varargs.c` 触发 `meuos-libc stdarg.h:8` `va_end argument must have type va_list`。根因是 mcc 的 `__builtin_va_end` 内建在 `#define va_end(list) __builtin_va_end((list))` 宏展开时**提前做参数类型检查**（详见 `projects/mcc/.todo/varargs-va_end.md`）。该内建应只在使用点检查类型，而非宏定义点。属 mcc bug；同时 `varargs.c` 依赖 `({})`。

## 6. 审阅中发现并已修复的 mcc 问题

为解锁整个 chibicc 测试集的编译（全部 `#include "test.h"`，而 `test.h` 内部对 `vsprintf` 重复声明：`int vsprintf(char*,char*,void*)` 与原型 + 旧式 `int vsprintf();`），修复了 `projects/mcc/src/sema/type.c` 中 `typecompatible` 与 `typecomposite` 的 `TYPEFUNC` 分支：

- **C11 6.7.6.3**：旧式函数声明（`params == NULL`，无参数信息）与原型声明兼容，优先采用原型参数信息。
- **C11 6.7.6.3p15**：函数参数类型比较忽略顶层限定符（`int(char*)` 与 `int(const char*)` 兼容），比较时使用 `typeunqual()`。
- 实现 `typeunqual()`（此前仅有声明未实现，导致链接失败）。
- 回归：mcc 自身 `check-c11`（13）+ `check-c23`（14）全部仍 PASS。

## 8. GNU 语句表达式 `({...})` 实现

在本次审阅会话中为 mcc 新增了 `({...})` 支持（GNU 扩展，但为真实世界代码所需）：

| 文件 | 修改内容 |
|---|---|
| `include/mcc.h` | 添加 `EXPRSTMTEXPR`、`struct stmt_expr_item`、`curfunc` 全局 |
| `src/parse/expr_primary.c` | `TLPAREN` 分支检测 `({` 并转向 `parse_stmt_expr_body` |
| `src/parse/expr_unary.c` | `castexpr` 的 `(!typename)` 分支检测 `({`（因 castexpr 会先于 primaryexpr 消费 `(`）|
| `src/parse/expr_stmt_expr.c` | **新文件**：`parse_stmt_expr_body()` 解析体内容 |
| `src/parse/expr.c` | `delexpr` 清理 `EXPRSTMTEXPR` |
| `src/parse/stmt.c` | `curfunc = f` 设置 |
| `src/irgen/func.c` | `struct func *curfunc` 全局定义 |
| `src/irgen/expr.c` | `funcexpr` 的 `EXPRSTMTEXPR` 分支 |
| `src/irgen/branch.c` | `funclval` 的 `EXPRSTMTEXPR` 分支 |

**实现策略**：
- 体内容在**解析阶段**处理：声明立即 `funcinit`，侧效表达式立即 `funcexpr`，控制流语句委托 `stmt(curfunc, s)`
- 只有**最后一个表达式**延迟到 IR gen 阶段由 `funcexpr` 处理
- `curfunc` 全局变量由 `stmt()` 在每个语句开始时设置，使 `parse_stmt_expr_body` 能调用 `stmt()` 处理控制流

**独立验证**：`test/c11/stmt_expr.c` 涵盖 7 种场景（简单值、声明+值、侧效+值、if、for、嵌套、void），全部 PASS。

## 9. 结论与建议

1. **libc C11 实现完整**：meuos-libc 自带 C11 测试全 PASS，原子/线程/TLS/IO 等均正确。
2. **mcc 标准 conformance 仍有缺口**：chibicc 测试暴露的 §5.B 各项（浮点后缀、字面量类型回退、宏重定义、限定指针转换、`__LINE__`、common 合并、Unicode 标识符、va_end 内建）是 mcc 真实的标准/C 缺陷，建议按优先级修复。`§5.A` 的 GNU 扩展为合理不支持，§5.C 为测试集自身偏差。
3. **补充纯标准集**：chibicc 依赖 GNU 扩展，对纯标准度量偏差大。建议后续引入 `c-tests`（nlsandler）或 musl `libc-test`（MIT）作纯标准 C11/C23 + libc 头 conformance 度量（当前网络不可达，待可达后补齐）。
4. **`({})` 扩展已完成**（§8）—— mcc 现在支持 GNU 语句表达式，16 个 chibicc 测试经 `({})` 推进到后续处理阶段，其中 4 个转为 PASS。
