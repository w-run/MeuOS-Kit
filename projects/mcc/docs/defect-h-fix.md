# 缺陷 H 根因分析与修复方案

> 调研人：planner-h（mcc-mxx-cont）
> 调研日期：2026-08-02
> 状态：只调研，未改业务代码。修复建议供 worker-cpp 实施。

## 缺陷描述

`c.A::f()` 限定成员调用 + 虚函数组合未绕过虚表：应返回 1（`A::f` 静态绑定），实际返回 3（走了虚表，命中 `C::f`）。

```cpp
class A { public: virtual int f() { return 1; } };
class B : public A { public: int f() { return 2; } };
class C : public B { public: int f() { return 3; } };
int main(){ C c; return c.A::f(); }  // 期望 1，实际 3
```

## 相关机制

### 1. 限定成员调用（35a6ede）

`src/c/parse/expr_postfix.c` 的 `TPERIOD`/`TARROW` 分支处理 `obj.Class::member`：

- `expr_postfix.c:399-433`：检测 `.`/`->` 后的 tag 是对象的基类（`cpp_is_derived`），若紧跟 `::` 则：
  - 计算基类子对象偏移 `cpp_base_offset(t, qt)`，把 `r`（this 指针）重定向到基类子对象（`expr_postfix.c:408-416`）；
  - `t = qt`（限定类），置 `qualified = true`（`expr_postfix.c:418-419`），进入 `member_lookup`。
- `expr_postfix.c:451-457`：`qualified` 为 true 时用 `cpp_qualified_member(t, name, &offset)` 在限定类自身作用域查找成员（优先本类成员，其次递归基类），使 `c.A::get()` 命中 `A::get` 而非 `C::get`。

### 2. 虚函数/虚表（7442b6c）

- `src/cpp/parse/cpp_parse.c:3203` `cpp_make_vcall()`：构造间接调用表达式
  `*(fn_type **)((*(void **)thisp) + vslot * 8)` —— 从对象 vptr 取虚表地址，按 `vslot` 槽取函数指针，间接调用。
- `expr_postfix.c:493-515`：成员是虚函数（`m->is_virtual`）时无条件走 `cpp_make_vcall`。

## 根因

**`expr_postfix.c:493` 的 `if (m->is_virtual)` 分支没有检查 `qualified` 标志。**

当 `c.A::f()` 走到 member 查找时：

1. 限定路径已生效：`qualified = true`，`t = A`（`expr_postfix.c:418`），`thisp` 已重定向到 A 子对象。
2. 成员 `m` 由 `cpp_qualified_member(A, "f", ...)` 取到，是 A 声明中的虚函数（`m->is_virtual == true`）。
3. 但 `expr_postfix.c:493` 只看 `m->is_virtual`，不看 `qualified` → 仍进入 `cpp_make_vcall` 分支 → 通过 vptr 间接调用。

由于 C 的虚表中 `f` 槽被 `C::f`（override）占用，间接调用命中 `C::f`，返回 3。

**结论**：C++ 语义规定**显式限定调用（qualified-id）应强制静态绑定**（`[class.virtual]`：虚函数经限定名调用时按非虚方式调用）。当前实现限定路径只修正了成员查找和 this 偏移，却漏掉了对虚分派的压制。

## 修复方案

### 最小改动点

`src/c/parse/expr_postfix.c:493`：

```c
// 改前
if (m->is_virtual) {
// 改后
if (m->is_virtual && !qualified) {
```

`qualified` 为 true 时不再走 `cpp_make_vcall`，落入后续**静态调用路径**（`expr_postfix.c:516-560`）：

- `cpp_mangled_name(t, m->name, mname, ...)` 生成 `A_f`（t 已是限定类 A）；
- `scopegetdecl(t->scope, "A_f")` 取 A::f 的降级自由函数符号；
- `thisp` 为已重定向到 A 子对象的指针（offset 由 `cpp_qualified_member` 计入，`expr_postfix.c:545-551`）；
- TLPAREN 调用降级 prepend this → `A_f(&c 的 A 子对象)` → 返回 1。

### 不改动点

- `cpp_parse.c:358` `cpp_member_ident` 的 `m->is_virtual` 分支（方法体内**裸成员名** `f()` 调用）语义正确：裸名是未限定虚调用，应动态分派，不要加 `!qualified`。
- 方法体内 `this->A::f()` 走的是 `expr_postfix.c` 的 `TPERIOD`/`TARROW` 分支（`this->` 是显式成员访问），由同一修复覆盖，无需额外改动。
- `cpp_make_vcall` 本身无需改动。

### 修复后行为

| 表达式 | 语义 | 修复后 |
|:--|:--|:--|
| `c.f()` | 未限定虚调用 | 3（动态分派，不变） |
| `c.A::f()` | 限定静态绑定 | 1 |
| `c.B::f()` | 限定静态绑定 | 2 |
| `c.C::f()` | 限定静态绑定（本类） | 3 |
| 方法体内 `this->A::f()` | 限定静态绑定 | 1 |

## 验证步骤

### 复现（缺陷存在时）

```sh
cat > /tmp/defect_h.cc <<'EOF'
extern int printf(const char *, ...);
class A { public: virtual int f() { return 1; } };
class B : public A { public: int f() { return 2; } };
class C : public B { public: int f() { return 3; } };
int main(){ C c; int r = c.A::f(); printf("r=%d\n", r); return r != 1; }
EOF
./m++ --specs=host -o /tmp/defect_h /tmp/defect_h.cc && /tmp/defect_h
# 修复前：r=3, exit=1
# 修复后：r=1, exit=0
```

### 修复后回归（已实测通过）

```sh
# 组合验证：限定+动态分派+各层限定+方法体内限定
# 全部断言通过后 exit=0
./m++ --specs=host -o /tmp/defect_h2 /tmp/defect_h2.cc && /tmp/defect_h2
./m++ --specs=host -o /tmp/defect_h3 /tmp/defect_h3.cc && /tmp/defect_h3

# 既有测试套件
make check-cpp-virtual          # 虚函数端到端（17 断言）PASS
./m++ --specs=host -o /tmp/qualified_member test/cpp/qualified_member.cc && /tmp/qualified_member
./m++ --specs=host -o /tmp/qualified_threehop test/cpp/qualified_threehop.cc && /tmp/qualified_threehop
./m++ --specs=host -o /tmp/vfun_combos test/cpp/vfun_combos.cc && /tmp/vfun_combos
```

> 注：`virtual.cc` 依赖 `stdio.h`，host specs 下报找不到头文件，应走 `make check-cpp-virtual`（specs=meuos）验证，已 PASS。

## 备注

- 本报告对应的修复由 worker-cpp 实施；调研期间在本地临时验证过修复（`expr_postfix.c:493` 加 `!qualified`），验证后已还原源码，未保留代码改动。
- 本次只新增 `docs/defect-h-fix.md` 报告文件，未触碰 worker-cpp 的 P2 在途改动（`ir.h`/`passes.c`/`spill.c`/`slotmerge.c`）。
