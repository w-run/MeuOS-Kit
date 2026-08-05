# 全面验收报告（Acceptance）

> 分支：`worktree-mxx-work`，HEAD `a88b1c9`。日期：2026-08-02。
> 目标：确认当前分支状态可作为稳定基线（goal「完全自举、端到端 pass」）。
> 范围：只验证不改码；问题记录不擅自修复。

## 1. verify-all 全量：PASS 6/6

| 环节 | 结果 |
|:-----|:-----|
| make check | PASS |
| make check-mir | PASS |
| make check-cpp（lex/func/neg） | PASS |
| check-c99（--specs=host 回退） | PASS |
| check-c11（--specs=host 回退） | PASS |
| make check-sysroot-static（自举） | PASS |

## 2. 自举产物运行验证：PASS

用宿主 mcc 静态自举构建 self-mcc / self-m++（/tmp/selfmcc，`--sysroot=sysroot/x86_64`），
再用自举产物编译运行真实程序：

| 程序 | 内容 | 结果 |
|:-----|:-----|:-----|
| fib.c | 递归斐波那契 fib(20) | self-mcc 编译 + 运行 = 6765 ✓ |
| cpp_prog.cc | 类 + 继承 + 虚函数（虚分派 43）+ 模板 max2/Box | self-m++ 编译 + 运行 = all OK ✓ |

自举产物功能完整（C + C++ 类/继承/虚函数/模板）。

## 3. 6 架构交叉验证：PASS 6/6

`/tmp/verify-matrix6.sh`（适配 mxx-work）扩展矩阵：递归/循环/switch/字符串/指针/
结构体/浮点/大数组程序，qemu 运行：

```
[PASS] x86_64  [PASS] i386  [PASS] aarch64
[PASS] riscv64  [PASS] loongarch64  [PASS] arm
==== 矩阵汇总: PASS=6 FAIL=0 ====
```

## 4. check-cpp 完整：PASS

`make check-cpp`（check-cpp-lex + check-cpp-virtual + check-cpp-func + check-cpp-neg）
exit=0，test/cpp/ 40 个测试全绿（含 concepts/lambda/模板/虚函数/移动语义/CTAD/
if constexpr/三向比较等）。

## 5. 发现的问题（非回归，记录待评估）

均为 **m++ 的 C++ 子集能力边界**（宿主 m++ 与自举 self-m++ 表现一致，非自举缺陷）：

1. **构造函数初始化列表**不支持：`Base(int v) : val(v)` → 「no type in struct member declaration」。
2. **`override` / `final` 说明符**不支持。
3. **`new` / `delete`** 动态分配不支持（`undeclared identifier: new`）。
4. **`Base::get()` 限定成员调用**不支持（歧义/成员指针错误）。

这些不影响现有 test/cpp 套件（测试均规避）；已在 C23/C++ 验收中标记 m++ 子集边界，
建议作为后续 C++ 增强项评估（worker-cpp 方向）。

## 结论

**当前分支状态可作为稳定基线**：6/6 验收全过、自举产物端到端可用、6 架构扩展矩阵
全绿、C++ 套件完整。m++ 能力边界（4 项）不阻塞基线，记录待后续增强。

> 基线确认：HEAD `a88b1c9`（worktree-mxx-work，与 origin 同步）。
