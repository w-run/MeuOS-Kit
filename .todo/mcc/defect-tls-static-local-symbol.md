# mcc TLS 局部静态符号不一致缺陷

> 来源：2026-08-04 接手审计（大喵指示验证实际进度）
> 严重度：🔴 高（阻塞 meow `make check`）
> 组件：mcc（`_Thread_local` 代码生成）

## 现象
`projects/meow` 的 `make check` 失败（rc=2），链接报：
```
/usr/bin/ld: build/engine/graph.o: in function `expand_path':
(.text+0x2a): undefined reference to `buffer'
/usr/bin/ld: (.text+0x49): undefined reference to `buffer'
collect2: error: ld returned 1 exit status
```

## 根因
mcc 对函数内 `static _Thread_local` 变量的代码生成**符号名不一致**：

源码 `projects/meow/src/engine/graph.c:31`：
```c
static _Thread_local char buffer[RECIPE_MAX];  // 函数 expand_path 内的静态 TLS
```

mcc 生成的汇编（`mcc -S`）：
```asm
.section .tbss,"awT"
    ...
.Lbuffer.2:              # ← 定义处：局部符号 .Lbuffer.2
    ...
expand_path:
    ...
    leaq buffer@tpoff(%rax), %rax   # ← 引用处：全局符号 buffer
```

**定义处用 `.Lbuffer.2`（局部符号），引用处用 `buffer`（全局名）**，链接器找不到 `buffer` → `undefined reference`。

## 复现
```sh
cd projects/meow
export MEUOS_SYSROOT=/workspace/MeuOS-Kit/sysroot
make check  # rc=2, undefined reference to `buffer`
# 定位汇编：
../mcc/mcc --specs=meuos --sysroot=$MEUOS_SYSROOT --nostdlib \
  -I../meuos-sysroot/include -Isrc/include -Isrc -Isrc/util \
  -S -o /tmp/graph.s src/engine/graph.c
grep -nE "\.Lbuffer|buffer@tpoff|\.tbss" /tmp/graph.s
# 定义: .Lbuffer.2  vs  引用: buffer@tpoff  → 不一致
```

## 修复方向
- mcc 后端 emit TLS 局部静态变量时，**定义处与引用处符号名必须一致**
- 对照非 TLS 的 `static` 局部变量处理（应已正确，参考其符号命名规则）
- 检查 `x86_64_emit.c`（及其他架构 emit）对 `static _Thread_local` 的符号发射逻辑
- 涉及 worker-deployment.md §3 bella TLS（1a1d599）/ chloe TLS（9430114）工作——TLS 全局符号已修，**局部静态 TLS 符号**未覆盖

## 验收标准
- [ ] mcc 对 `static _Thread_local` 变量的定义符号与引用符号一致
- [ ] `projects/meow` `make check` rc=0
- [ ] `meow` 二进制成功链接并运行
- [ ] `verify-all.sh` 仍 19/19（无回归）
- [ ] 交叉验证：aarch64/riscv64/arm/i386/loongarch64 同类 TLS 局部静态符号也一致

## 备注
- verify-all 19/19 未含 meow 链接，故此缺陷被掩盖
- 此缺陷可能也影响 libc（若 libc 有 `static _Thread_local` 局部变量），修复后需复测 libc
- 修复后建议把 meow `make check` 纳入门禁或周期审计
