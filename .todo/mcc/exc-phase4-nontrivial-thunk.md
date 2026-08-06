# 异常阶段4 增强：非 trivial 类 copy/dtor thunk 合成

> 状态：✅ 已闭环（2026-08-07 mpp-exc-p4-worker，commit 1b1d9976 + 55797045）
> 关联：`exc-phase4-object-payload.md`、`_meuos_exc_throw_obj(int,size,align,copy,dtor,offset,obj)`

## 缺口

当前 mcc 异常对象 payload 只对**字节可拷贝类**（trivial/无用户 dtor）用 memcpy 承载（`_meuos_exc_throw_obj` 传 `copy=NULL/dtor=NULL`）。有用户**拷贝构造/析构**的类：
- 走标量回退（`exc_has_trivial_copy/dtor` guard）——对象不携带，catch 参数未重建。
- 需要 mcc **合成 thunk** 传入运行时：
  - `copy(dst, src)`：调用 T 的拷贝构造（把 src 拷进 dst，堆上对象）。
  - `dtor(self)`：调用 `~T()`（运行时 caught_free 时销毁）。

## 方案要点

- mcc 前端为类 T 生成编译器合成函数 `__meuos_exc_ms_copy_T(void*, const void*)` / `__meuos_exc_ms_dtor_T(void*)`，把 T 的拷贝构造/析构 thunk 化。
- `cpp_exc_throw_call` 类分支传 `copy=thunk`/`dtor=thunk`（非 trivial 时）。
- 需 mcc 函数合成机制（`mkfunc` + emit body）；脆前端回归风险——**后置**，先锁当前实用子集。

## 验收（届时）

- 有用户 dtor/拷贝构造的类 `throw MyClass` 全链：构造→throw_obj→copy 构造进堆→catch 重建→析构→caught_free→exit 0。
- `exc_object_gate` 扩展覆盖非 trivial；verify 24/24 不破 + try_catch SKIP 0。
