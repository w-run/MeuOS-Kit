# 工具链缺 .init_array 数据钩子 → compat 归档无法在启动挂构造器/跨归档写

> 状态：🔄 待专项
> 发现：2026-08-05（libc-worker，item b/c）
> 位置：meuos-toolchain（crt1.S + mt/ld）+ meuos-libc（entry contract）
> 优先级：中（根治后可将 program_invocation_* 数据移回 compat）

## 背景

此为导致 `program_invocation_name/_short_name` 数据被迫落 core（`startup.c`,
commit 2b80177）的**根因工具链能力缺口**。

## 现状（3 个实证限制）

1. **crt 无 `.init_array`**：所有 6 架构 `crt1.S`（x86_64/i386/aarch64/arm/
   riscv64/loongarch64）都没有 `.init_array`/`.fini_array` 处理，也没有
   `__libc_start_main` wrapper → 一个 opt-in 的 compat/eval 归档**没有启动钩子**
   来执行构造器（`__attribute__((constructor))` 或隐式 `_init`）。
2. ~~**core 对未定义 weak 符号的「写」被工具链打爆**~~ **✅ 已修**（2026-08-05）：`fill_got()` 对 unresolved weak 写 0 而非报错。.
3. **GAS `.set` 别名跨 archive 不可链接**：`program-name.S` 里
   `.set program_invocation_name, __progname` 不产生可被消费者解析的公开符号；
   引用方 unresolved（`nm` 无定义）。GCC `__attribute__((alias))` 又要求同 TU 定义。

## 根治方向（未来专项）

- 给 crt1.S + mt/ld 增加 `.init_array`/`.fini_array` 支持，让 opt-in 归档可挂构造器。
- 或实现 libgcc/compiler-rt 式的「启动数据表」：core 暴露 `__meuos_startup` 之后
  遍历一个由链接器填充的 init 指针表。
- ~~修好上传符折叠（unresolved weak → 0）~~ **✅ 已修**后将 program_invocation_* 数据移回 compat，
  与 musl 的 libc-start 布局对齐需再评估。

## 关联

- 已落地的过渡方案：core `src/startup.c` 定义并填充 program_invocation_*；compat
  只保留 `program-invocation.h` 声明头（opt-in 语义不变）。详见 ARCHITECTURE.md
  入口契约节。
