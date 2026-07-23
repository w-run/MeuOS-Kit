<!--
priority: P3
status: done
done_ts: 2026-07-23
note: targ.c typevalist 改 struct，跨函数 va_list 永久参考
-->

# i386 printf %d 跨函数 va_list — 已修复（永久参考）

> Update 2026-07-23：根因定位并修复，qemu 端到端验证已通过。本文件
> 保留为根因分析参考文档，便于未来其他 32 位 target（如 armv7）参考。

## 根因

**不在** `src/target/i386/i386_sysv.c`，而在 mcc 前端的 `src/sema/targ.c`：
i386 的 `typevalist` 此前被定义为 4 字节裸 `TYPEPOINTER`（与 amd64/
riscv64/loongarch64 的 struct 型 va_list 不一致）。

### 根因链

1. `src/parse/expr_primary.c` 的 `BUILTINVAARG` 分支有：
   ```c
   if (typeadjvalist == targ->typevalist)
       e->base = mkunaryexpr(TBAND, e->base);  // 取 va_list 地址
   ```
   该分支负责把 `va_arg`/`va_start` 的 `ap` 操作数**取地址**。

2. 裸指针型 va_list 经 `typeadjust` 后得到**不同**的类型，于是
   `typeadjvalist == targ->typevalist` 不成立，该分支被跳过——
   `ap` 以**指针值 P** 传入后端，而非地址。

3. `selvaarg` 却把 `i->arg[0]` 当作**地址** 用：
   - `load [ap]` 读 P、`store [ap]` 回写。
   - 当 `ap` 被 rega 放进寄存器时，`load`/`store` 走
     `movl (%reg)` 二次解引用：把进阶后的指针写进了 `*P`
     （破坏变参本身），且从未更新 `ap` 自己的槽位 → 跨函数场景崩溃。

### 修复

把 i386 `typevalist` 改为 4 字节 `TYPESTRUCT { void *__p; }`
（size=4, align=4，与 amd64/riscv64/loongarch64 对齐）。此时
`typeadjvalist == targ->typevalist` 成立，TBAND 取地址生效，`ap`
在所有场景下都成为**地址**；`selvaarg`/`selvastart` 原逻辑无需改动。

### 验证

- `make -C mcc check-i386` 通过。
- `va_test.c` IR/asm 对比：`helper`（跨函数）与 `sum`（同函数）的
  `va_arg` 生成完全一致。
- `test/i386/runtime_va.c` 的 `vsum`（跨函数 va_list 转发）已在
  `runtime.sh` 和 `qemu-runtime.sh` 双门禁中通过端到端数值验证。

> 本文件为根因分析永久参考文档，非待办项。

## 验收标准

<!-- TODO(main session): fill in concrete commands. -->

```
make -C projects/meuos-libc check
```

