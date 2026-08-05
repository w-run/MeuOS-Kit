# 异常阶段4 增强：基派生切片（catch Base 捕获 throw Derived 对象子对象）

> 状态：🔄 后置待排（2026-08-05 异常4 第2段 catch 侧完成 + 4b 定档后登记）
> 关联：`exc-phase4-object-payload.md`、`_meuos_exc_throw_obj(...,offset,...)`

## 缺口

`catch(Base)` 捕获 `throw(Derived)` 时，类型匹配（阶段3 cpp_is_derived）已支持，但对象**子对象切片**未做：
- 运行时 f63bff1 `_meuos_exc_throw_obj` 把 `offset_to_base` 置 `(void)`（later increment）。
- catch 参数应指向 Derived 对象内的 Base 子对象（`base_ptr = derived_ptr + offset`）。

## 方案要点

- **libc 侧小增量**：f63bff1 `_meuos_exc_throw_obj` 的 `offset` 落地——堆上 keep 完整对象 + 记录 offset；`_meuos_exc_caught_obj()` 按需返回子对象指针。
- 或 mcc caught 端按 offset 自行切片（advance 指针 + 相应 base dtor）。
- offset 由 mcc 传（`exc_base_offset`，当前返回 0）。

## 验收（届时）

- `struct B{}; struct D:B{}; try{throw D;} catch(B&)` 捕获到 B 子对象（非整 D）。
- 多级基链、切片正确（mcc meta offset + libc 调整）。
- verify 24/24 不破。
