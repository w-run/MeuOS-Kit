# MIR 类型完备性矩阵

> B.1.8 审计产物：C 前端类型系统（`include/mcc.h` `typekind`）↔ MIR 类型
> （`include/mir.h` `MType`）的覆盖对照。目标：MIR 可表达 C 全类型。

## 标量类型

| C typekind | MIR MType | 说明 |
|:-----------|:----------|:-----|
| TYPEVOID | MT_VOID | void |
| TYPEBOOL | MT_I8 | _Bool 以 i8 表达（ABI 层可窄化） |
| TYPECHAR | MT_I8 | char/signed char/unsigned char |
| TYPESHORT | MT_I16 | short |
| TYPEINT | MT_I32 | int |
| TYPEENUM | MT_I32 | 枚举按 int |
| TYPELONG | MT_I64 / MT_I32 | LP64→i64；ILP32(arm/i386)→i32，由 targ 决定 |
| TYPELLONG | MT_I64 | long long |
| TYPEFLOAT | MT_F32 | float |
| TYPEDOUBLE | MT_F64 | double |
| TYPELDOUBLE | MT_F64 | long double 按 double（现状降级） |
| TYPEPOINTER | MT_PTR | 指针 |
| TYPENULLPTR | MT_PTR | nullptr_t 按指针 |
| TYPEBITINT | MT_I64（上限） | _BitInt(N)，N≤64 折叠；>64 待扩展 |
| TYPEATOMIC | 基础 MType | _Atomic 载荷类型 |
| TYPEDECIMAL32/64/128 | 待扩展 | _Decimal 系列（C23），暂以 f64 占位 |

## 聚合类型

| C typekind | MIR 表达 | 说明 |
|:-----------|:---------|:-----|
| TYPEARRAY | MTypeDesc is_array | elem_type/elem_desc + nelem；VLA 用 MOP_ALLOCA + 动态长度 |
| TYPESTRUCT | MTypeDesc (非 union) | MField[] 逐字段偏移/类型/位域 |
| TYPEUNION | MTypeDesc is_union | 字段 offset 均为 0 |
| 位域 | MField.bitoff/bits | 已入 MField |
| 柔性数组 | MTypeDesc 尾字段 | struct 末端 [0]/[] 元素，size 不含 |

## 扩展槽（C++ 预留）

- `MIns.extra`：C++ 前端挂 MOP_EXTRA 派生指令（虚调用/异常/类型信息）。
- `MTypeDesc.ext`：C++ 前端挂 vtbl 指针 / RTTI 元数据。
- `MVal` 无新字段需求；`MRef` 支持值/常量双引用。

## 结论

标量全绿；聚合（struct/union/array/位域/柔性数组）全绿；VLA 经 ALLOCA 路径
可表达；`_Decimal*` 与 `_BitInt(>64)` 标记"待扩展"（C23 特性），**不阻塞
C++11 主线**（m++ 目标标准 C++98→23 不含 _Decimal/_BitInt）。扩展槽
（MIns.extra / MTypeDesc.ext）空载实验已通过（见 docs/mir-spec.md §3.8）。

> 状态：✅ B.1 阶段 100% 覆盖（除标注非阻塞待扩展项）；B.6 验收项 1 达标。
> 验证：`make check-mir-types` 含 mssa_check（SSA 门禁）与 test_extra（扩展槽）。
