---
name: mxx 缺陷 K/M/N
description: m++ 新缺陷 K（concept 递归深度16上限）/M（未命名参数 ctor 编译段错误）/N（数组 new 元素 stride，已修复）
type: project
---

2026-08-03：并行测试任务（concepts 组合 + 数组 new/delete 边界）暴露的 m++ 缺陷：

- **缺陷 K**：concept 约束递归深度上限 = 16。链 `C{n}=C{n-1}<T>&&...` n≤16 正常，n≥17 编译失败且报错误导（`invalid operands to '<' operator`，失败位置随链长漂移）。待修。
- **缺陷 M**：带**未命名参数**的构造函数使 m++ 编译期段错误（rc=139），任意构造上下文（栈/标量 new/数组 new）均触发；命名参数 `B(int v)` 正常、自由函数未命名参数正常。最小复现 `class B{B(int){};int val;}; B b(3);`。待修，canary 测试 `test/cpp/new_delete_unnamed_param.neg.cc`。
- **缺陷 N**：`new T[n]` 类元素构造 stride 错误——IR TADD 不缩放指针，`&arr[i]` 按字节偏移而非 `i*sizeof(T)`，元素宽于 1 字节时构造/析构位置错误（`T(){val=5}` → arr[0]=0x050505）。**已修复**（worker-cpp 754b437，显式 `i*sizeof(T)`），实测 `5 5 5`。

**Why:** 测试补充（b2da695 concepts 组合 + 329de75 数组 new 特性）的边界用例暴露；测试由 worker-opt 主提交（b6c955e/b001057/b033750/46db62e）。
**How to apply:** defect M/K 修复后需复测（canary + 16/17 层链）；defect N 修复已验证。相关文档 .issues/0802.md（46db62e）。