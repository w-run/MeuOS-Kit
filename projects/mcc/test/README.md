# mcc/m++ 测试组织说明

本目录是 mcc（C11/C++ 编译器）的测试集合。本文档说明新增测试的
组织方式与收集机制。

## 运行方式

- 一键验收：`sh test/verify-all.sh`（顶层门禁，见任务 #2）
- 分目标运行（在 `projects/mcc/` 下）：

| 目标 | 覆盖 | 收集方式 |
|------|------|----------|
| `make check-c99` | C99 功能回归 | glob `test/c99/*.c` |
| `make check-c11` | C11（atomic/thread_local/varargs） | glob `test/c11/*.c` |
| `make check-c23` | C23 | glob `test/c23/*.c` |
| `make check-cpp-func` | m++ 正向功能 | glob `test/cpp/*.cc`（排除 `.neg.cc` 与 `virtual.cc`） |
| `make check-cpp-neg` | m++ 负向（应被拒绝） | glob `test/cpp/*.neg.cc` |
| `make check-cpp-virtual` | 虚函数/虚表（需 sysroot） | 固定 `test/cpp/virtual.cc` |
| `make check-mir-passes` | MIR fold/copy/gvn/dce 单元测试 | 固定 `test/mir/pass_test.c` |

## 自动收集（glob）约定

- `check-c99` / `check-c11` / `check-c23` / `check-cpp-func` /
  `check-cpp-neg` 均用 glob 自动收集，**新增测试文件放入对应目录
  即被自动执行**，无需改动 Makefile。
- 负向测试命名必须为 `*.neg.cc`（`check-cpp-neg` 编译期望失败；
  能编译成功即视为测试失败）。
- `test/cpp/` 下的正向 `.cc` 必须是 freestanding 程序：用
  `./m++ --specs=host` 编译链接宿主 libc，每个断言返回独立非零
  退出码（exit 0 = 全部通过），可直接用标准函数（`extern`
  声明 `puts`/`printf` 等）但不得依赖系统头文件。

## 单元测试（非 glob）

- MIR pass 单元测试集中在 `test/mir/pass_test.c`：用 `mir.h` API
  构建小函数、跑 pass、检查指令结构。新增用例写为
  `test_xxx()` 并在 `main()` 中注册，编译运行 `make check-mir-passes`。

## `test/cpp/pending/` —— 已知缺陷复现

- 该目录存放 m++ 已知缺陷/限制的**最小复现 + 文档**（文件头注释
  记录当前行为、已确认的正常对照路径、根因假设、期望行为）。
- 不参与任何 glob 收集（子目录不匹配 `test/cpp/*.cc`），因此不会
  挂断 CI；实现侧修复后把文件移出 pending 并接入对应 check 目标。
- 2026-08-02 已登记缺陷（详见各文件头注释；已修复项移出 pending 并接入 check-cpp-func/check-cpp-neg）：
  - `value_param_member_call.cc` — size-0 类按值传参段错误
  - `ns_limits.cc` — namespace 四项限制（嵌套类路径/引用参数
    函数/类返回值拷贝/变量符号冲突）
  - 已修复并移出：`free_func_overload.cc`（自由函数重载，
    → `test/cpp/free_func_overload.cc`）、`ctor_base_dtor.cc`（继承
    析构链，→ `ctor_dtor_order.cc`）、`static_void_method.cc`
    （static void 方法，→ `static_void_method.cc`）
- 2026-08-02 清理（gp-2）：删除 8 个早期复现测试——已被转正版或
  等效测试覆盖，无独立价值：
  - `ambig_addr.neg.cc` → `test/cpp/ambig_addr.neg.cc`（转正）
  - `multi_ambig.neg.cc` → `test/cpp/multi_ambig.neg.cc`（转正）
  - `free_operator.cc` → `test/cpp/free_operator.cc`（转正）
  - `global_dtor.cc` → `test/cpp/global_dtor.cc`（转正）
  - `method_addr.cc` → `test/cpp/method_addr.cc`（转正）
  - `ctor_ident_args.cc` / `ref_overload.cc` / `ref_var.cc`
    → `test/cpp/ref_ctor.cc`（引用变量、值/引用重载、带标识符
    实参构造链已被综合覆盖）
  - 保留 `struct_multinher.cc`：struct 多继承（`struct D : A, B`）
    仍未修复（报 "expected '(' or identifier"），无等效测试，
    作已知缺陷复现继续保留

## 新增测试清单（2026-08-02，worker-test）

- m++ 正向：`vfun_combos.cc`、`tpl_overload.cc`、`ctor_dtor_order.cc`、
  `ns_scope.cc`、`static_member.cc`（均 glob 自动收集）
- C99：`float_expr.c`、`large_array_init.c`、`bitfield_ops.c`
- MIR：`pass_test.c` 新增 fold 边界（sdiv/rem 负操作数、浮点比较、
  shift-zero）与 DCE 副作用（CALL 保留、LOAD 移除）单测
