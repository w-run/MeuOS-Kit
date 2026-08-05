# m++ 异常阶段4 —— 类类型对象 payload（throw/catch 完整对象生命周期）

> 关联：`meuos_exc.h` 运行时（setjmp/longjmp 一阶基础）、`cpp_newdel.c` 前端降级。
> 现状（阶段1-3）：payload 是 `(typecode:int, value:u64)` 只走 8 字节 slot；
> **类类型异常 member 值不传递**（`cpp_exc_throw_call` 对 TYPESTRUCT/UNION 把 slot 置 0，注释为 documented limitation）。
> 阶段4 目标：`throw SomeClass(...)` 携带对象，catch 按类重建对象 + 析构，含基/派生切片 + 堆/栈生命周期。

## 1. 问题与约束

当前 runtime 单 slot（`exc_typecode` + `exc_value:u64`）无法承载任意对象：
- 对象可达任意 size/align，非 8 字节。
- 对象有构造/析构（成员 + 基 + vtable/复杂类），`throw 表达式` 需先构造临时，异常传播期间析构临时，catch 处再构造 catch 参数，最后析构。
- 基/派生捕获：`catch(Base)` 匹配 `throw(Derived)`（阶段3 已按 typecode 注册表 + `cpp_is_derived` 展开做**类型匹配**），但对象**成员/子对象切片**未做（目前仅匹配类型，catch 参数 default-init 无真对象）。

纯 setjmp/longjmp 无法传播**任意栈对象**的生命周期（longjmp 跳过栈销毁），故对象 payload 承担必须放**堆**，由 runtime 持有，catch 消费后释放（`__cxa_throw` 同思路：`__cxa_allocate_exception` 堆分配 + `__cxa_throw`）。

## 2. 设计：对象 payload 进入异常

### 2.1 扩展运行时接口（给 libc-worker 的接口清单 —— 见文末 §5）

在保留现有 `_meuos_exc_throw(int, u64)` 基础上，**新增对象版**，并把 `_meuos_exc_frame`/persist 扩展为携带**对象指针 + 元数据**而非仅 u64：

```
_meuos_exc_frame {
	jmp_buf env;
	struct _meuos_exc_frame *prev;
	// 阶段4 新增（对既有 frame 追加成员，兼容旧版仅用前两个成员）
	void        *exc_obj;      // 指向 runtime 持有的堆上对象（若本次异常是对象）
	int          exc_typecode; // 移入 frame（仍可留 thread-local 兼容读取）
	u64          exc_value;    // 标量兼容
	const __meuos_exc_meta *exc_meta; // 对象类型元数据（size/align/copy/dtor/offset_to_base）
}

void _meuos_exc_throw_obj(
	int typecode,
	const __meuos_exc_meta *meta, // 类型元数据
	const void *obj);              // 指向 throw 表达式构造好的对象（前端临时）
                                   // runtime 负责: 按 meta->size/align 堆分配合 + 调 meta->copy(obj,dest)(按基偏移?) + 析构原 obj?
                                    // 或: 前端已调 meta->alloc()+meta->copy 放入 runtime buffer? 见 §2.2 分工
```

**关键分工分歧（需 libc-worker 拍板）**：谁做"堆分配 + 拷贝 + 析构原临时"？

- **方案 A（runtime 全权）**：前端只调 `_meuos_exc_throw_obj(tc, meta, &tmp_obj)`；runtime 分配堆 buffer、`meta->copy(dest, src)`、析构 src（临时）、挂 frame。catch 端调 `_meuos_exc_caught_obj()` 拿指针。**前端最简单**，但 runtime 需能析构"前端临时"（meta 提供 dtor）。
- **方案 B（前端分配 + 拷贝，runtime 仅挂链）**：前端 `meta->alloc()` + `meta->copy(heap,&tmp)` + 调 `_meuos_exc_throw_obj(tc, meta, heap)`；runtime 只存指针。catch 端消费后调 `meta->dtor+meta->free` 或 `_meuos_exc_caught_free()`。**前端更复杂**但 runtime 轻。

推荐 **方案 A**（贴近 `__cxa_allocate_exception/__cxa_throw` 语义：runtime 管生命周期），前端只需一次 `throw_obj` 调用。

### 2.2 `__meuos_exc_meta`（类型元数据，前端在 throw 处内联这些信息；libc 侧定义结构 + 用到的函数指针签名）

```
typedef struct __meuos_exc_meta {
	size_t size, align;            // 对象尺寸/对齐
	void (*copy)(void *dst, const void *src); // 拷贝构造 / 按位拷贝（见 §3 限制）
	void (*dtor)(void *self);      // 析构（trivial 型可为 NULL → 免析构）
	int   offset_to_base;          // 用于基捕获切片（§4）；0 = 无基
	// 可选：按位拷贝 vs 拷贝构造 —— trivial 类用 memcpy，非 trivial 用 ctor
} __meuos_exc_meta;
```

前端在 `throw T(obj)` 降级时生成一个该类型的 `meta`（可放本地 static 或直接常量内联），包含 T 的 size/align + 拷贝构造调用 + 析构调用 + 基偏移。

## 3. mcc 前端降级（本阶段可独立实现于 mcc-toolchain）

### 3.1 `throw T(obj)`（T 为类类型）

`cpp_exc_throw_call(t, e)` 对类类型改为：
1. 构造 meta 常量：`{ size = sizeof T, align = alignof T, copy = __meuos_exc_ms_copy_T, dtor = (trivial? NULL : __meuos_exc_ms_dtor_T), offset_to_base = ... }`。
   - `__meuos_exc_ms_copy_T` / `__meuos_exc_ms_dtor_T` 是 mcc 生成的**编译器合成函数**（拷贝构造/析构的 thunk，引向 T 的拷贝 ctor 与 ~T）。trivial 类可不生成而用 NULL 表"按位拷贝/免析构"。
2. 调 `_meuos_exc_throw_obj(cpp_exc_typecode(T), &meta, &e)`（`&e` = throw 表达式构造的临时地址）。

### 3.2 拷贝构造/析构 thunk（关键编译机器）

mcc 需能对类类型生成：
- `copy(dst, src)`：做 `dst.~T(); dst = src;`（trivial 直接 memcpy）；非 trivial 需调 T 的拷贝构造（`cpp_ctor_expr(t, dst_ptr, [src_ptr])` 路径，mcc 已有参考参数 ctor 处理 L128）。**若 T 用户定义了拷贝构造则用之；否则逐成员拷贝（含基 + 成员，走各自 copy）**。
- `dtor(self)`：调 `~T()``（成员 + 基析构，mcc 已有 `cpp_emit_ctor`-对偶的析构生成？——**需核对**：阶段4 若 mcc 尚无析构生成，先支持 trivial（NULL dtor 免析构）作为第一增量，非 trivial 列为后续）。

### 3.3 class catch 参数重建（catch 侧）

当前 catch 类参数 default-init（无 payload）。阶段4 改为：
- `catch (T e)`（T 类）：从 `_meuos_exc_caught_obj()` 拿 heap 对象，**在 catch 参数栈位置重建对象**：
  - `e = *caught_obj`（调用 T 拷贝构造，放到参数栈）；若 T 与抛类型不同（基捕获），需按 `meta->offset_to_base` 切片——runtime 应让 `caught_obj` 指向**该 catch 类型的对应子对象**（见 §4）。
  - 参数析构在 catch 体结束（局部作用域），由 mcc 已有局部析构机制（**需核对现支持程度**）。
- catch body 结束后，runtime 释放 heap 对象（`_meuos_exc_caught_free` 或 meta dtor+free）。

### 3.4 基/派生捕获 + 对象切片

阶段3 已按 typecode 注册表 + `cpp_is_derived` 做**类型匹配**（catch Base 匹配 throw Derived）。阶段4 对齐**子对象**：
- throw 携带完整 Derived 对象（meta 记录 Derived 布局，`offset_to_base` 指 Derived→Base 子对象偏移）。
- `_meuos_exc_caught_obj()` 对 catch 类型返回"该类型对应的子对象指针"：若 catch 类型 = 抛类型 → 原对象；若是基 → `base ptr = derived_ptr + offset_to_base`（runtime 需要知道 catch 类型对应的偏移，通过 meta 的基链或前端传入）。**第一增量**：单继承下将 `offset_to_base` 序列化为 meta，runtime 提供 `caught_obj(tc_catch)` 按基链返回正确子对象。

## 4. 生命周期

- **throw 侧**：`throw 表达式` 临时对象在调用 `throw_obj` 后（runtime 拷贝进堆）立即析构（前端在 throw_obj 调用点析构临时）。
- **传播**：heap 对象由 runtime 持有（挂 frame），跨 longjmp 存活。
- **catch 侧**：重建到参数栈，catch 体结束参数析构；之后 runtime 释放 heap（`_meuos_exc_caught_free`）。若未捕获 → abort（现状）。

## 5. libc 异常运行时扩展接口清单（交付 libc-worker）

给 libc-worker 实现的 libc 侧扩展（`meuos_exc.h` + `meuos_exc.c`）：

### 新增函数（对现有 `_meuos_exc_throw/...` 的补充，保存现有标量接口）

1. `_meuos_exc_throw_obj(int typecode, const __meuos_exc_meta *meta, const void *obj)` —— 对象版 throw；
   分工：**方案 A**：runtime 按 meta 堆分配+拷贝+析构源临时，挂 frame，longjmp。
2. `const void *_meuos_exc_caught_obj(void)` —— 取当前异常对象指针（= meta 调整后的子对象，依 §4）。
3. `void _meuos_exc_caught_free(void)` —— catch 消费后释放 heap 对象（若方案 A 且为堆）。由 mcc 在 catch 体结束/重抛时调。
4. `int _meuos_exc_kind(void)` 或复用 `_meuos_exc_caught_type()`：区分本次异常是标量(u64)还是对象（供 catch 通用路径）。
5. 扩展 `_meuos_exc_frame` 增加 `void *exc_obj; const __meuos_exc_meta *exc_meta;`（标量路径保持 `exc_value`）。

### 新增类型/符号

6. `typedef struct __meuos_exc_meta { size_t size, align; void (*copy)(void*,const void*); void (*dtor)(void*); int offset_to_base; } __meuos_exc_meta;` + 若有重抛重入需 `const __meuos_exc_meta *`/`void *` thread-local persist（`exc_meta/exc_obj`）。
7. 若选方案 B（前端分配），则需额外 `__meuos_exc_meta.alloc`/`free` 函数指针接口。

### 兼容性

- 现有 `_meuos_exc_throw(int,u64)`、`_meuos_exc_caught_type/_value`、`_meuos_exc_frame{env,prev}` 保持（阶段1-3 的 mcc 标量路径回归不破）。
- 新增成员追加到 frame 末尾（或持久化到 thread-local），旧 frame 前两成员布局不变。

## 6. mcc 侧落地范围（本项第1段，可独立验证）

在 mcc-toolchain 分支实现（无需等 libc）：
- `cpp_exc_throw_call` 类类型分支：生成 `meta` 常量 + `_meuos_exc_throw_obj(tc,&meta,&tmp)` 调用 + 临时析构。
- 生成拷贝 thunk（`__meuos_exc_ms_copy_T`）与析构 thunk（`__meuos_exc_ms_dtor_T`）（先 trivial 型：copy=memcpy/NULL dtor；非 trivial 拷贝构造/析构生成列为阶段4b）。
- class catch 参数：从 `_meuos_exc_caught_obj` 拷贝重建；body 结束析构参数 + `_meuos_exc_caught_free`（先 trivial；切片 offset 第一增量）。
- 对 stub 版 `__meuos_exc_meta`（还没 libc 实现时）用 mcc 侧临时头/内联定义验证降级**生成正确调用 + 链接独立**（可先以手写 stub 运行时测汇编/链接，等 libc 接口合入后真跑）。

**验收**：`throw MyObj(int)` 编译成 `_meuos_exc_throw_obj(tc, &meta, &obj)` + copy/dtor thunk 正确；用 stub 运行时独立链接验证生成调用形状；mcc verify 24/24 不破。

### 进度（至 2026-08-05，libc f63bff1 定档后）

已完成（commit 754f176 = 第1段，1d08c14 = 第2段 catch 侧）：
- 第1段：类 throw 降级 `_meuos_exc_throw_obj(tc,size,align,copy,dtor,offset,&tmp)`（trivial: copy/dtor=NULL → 运行时 memcpy/免析构）；仅当程序声明运行时走对象路径，否则回退零 slot 标量（无 undefined 引用）。`check-cpp-exc-object` 门禁。
- 第2段 catch 侧：`catch(T e)` 类参数从 `_meuos_exc_caught_obj()` 拷贝重建（guarded 在 `_meuos_exc_caught_is_obj()`，防标量回退类 NULL deref）+ 消费后 `_meuos_exc_caught_free`。运行时全链 exit 0（trivial 类 throw→catch 重建→free）。verify 24/24 + try_catch SKIP 0。

**剩余（4b，待定档）**：
- 非 trivial 类：`copy`/`dtor` thunk 合成 —— 有用户拷贝构造/析构的类需 mcc 生成 thunk 函数传入运行时（当前字节可拷贝类已 memcpy 承载；用户 ctor/dtor thunk 需 mcc 合成函数机制）。
- 基派生切片：`catch(Base)` 捕获 `throw(Derived)` 用 `base_offset` 调整对象指针 —— libc f63bff1 把 offset 置 `(void)`（later increment），需 libc 实现 offset 调整（或 mcc 在 caught 端按 offset 自行切片）。

