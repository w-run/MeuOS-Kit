# mcc (MIR-native) vs GCC 性能基准对比报告

日期：2026-08-03
编译环境：GCC 14.3.1；mcc MIR-native 默认（-O2/-O3），x86_64 宿主
测量：指令数 = `objdump -d | awk '/^[[:space:]]+[0-9a-f]+:/{c++}'`；运行时 = 3 次取中位

## 1. Benchmark 集（/tmp/bench/）

| 文件 | 覆盖 | 说明 |
|------|------|------|
| intloop.c | 整数密集 | 数组求和循环 + 算术累加（N=10^8） |
| fp_mat.c | 浮点密集 | 64×64 矩阵乘 + 20 阶多项式 |
| structs.c | 指针/结构 | 链表求和 + BST 插入/查找 |
| strings.c | 字符串 | 手写 strlen/strcmp/memcpy |
| recur.c | 函数调用密集 | fib 递归 + 尾递归 sumn + gcd |
| sortbench.c | 综合 | 插入排序 + 快速排序 |

全部 6 个基准在 mcc 与 gcc 下**输出一致**（正确性验证通过）。

## 2. 对比表（mcc vs gcc，O2 与 O3）

| bench | opt | mcc指令 | gcc指令 | 指令比 | mcc时间(s) | gcc时间(s) | 时间比 |
|-------|-----|---------|---------|--------|-----------|-----------|--------|
| intloop | O2 | 433 | 204 | 2.1x | 0.417 | 0.014 | **29.8x** |
| intloop | O3 | 435 | 271 | 1.6x | 0.435 | 0.015 | **29.0x** |
| fp_mat | O2 | 642 | 225 | 2.9x | 0.408 | 0.015 | **27.2x** |
| fp_mat | O3 | 656 | 679 | 1.0x | 0.444 | 0.014 | **31.7x** |
| structs | O2 | 757 | 245 | 3.1x | 0.592 | 0.165 | 3.6x |
| structs | O3 | 757 | 1850 | 0.4x | 0.586 | 0.193 | 3.0x |
| strings | O2 | 613 | 226 | 2.7x | 3.733 | 0.136 | **27.4x** |
| strings | O3 | 613 | 363 | 1.7x | 3.787 | 0.138 | **27.4x** |
| sortbench | O2 | 897 | 311 | 2.9x | 5.453 | 0.346 | **15.8x** |
| sortbench | O3 | 912 | 413 | 2.2x | 5.989 | 0.345 | **17.4x** |
| recur | O2 | 418 | 451 | 0.9x | 0.168 | 0.072 | 2.3x |
| recur | O3 | 418 | 503 | 0.8x | 0.174 | 0.070 | 2.5x |

**要点**：
- 指令数差距 0.8x–3.1x（大多 2–3 倍），但**运行时差距 2.3x–32x**。
- 运行时差距远大于指令数 → **单条指令执行效率**差（内存往返、冗余 mov、缓存不友好），不仅是指令多。
- structs（指针追访）差距最小（3.6x），纯计算/数组类差距最大（27–32x）。
- recur（递归）差距最小（2.3x），说明调用本身不是最大瓶颈，**循环体质量**才是。

## 3. 热点根因分析

### 热点 A：参数 slot 化 + 解引用间接（影响所有基准，最大根因）
`sum_arr` 循环里每个参数访问都是：
```
mov %rdi,%r10      ; 参数 a 的 slot 地址
mov (%r10),%rax    ; 解引用取 a
```
GCC 直接在 `%rdi` 用参数。MIR-native 把参数落地到栈槽后通过指针解引用访问
（`func_to_mir` 参数表示 + regalloc 无参数寄存器 hint），每次参数访问
多一次内存往返；循环内参数访问成热点。

### 热点 B：循环变量栈溢出（第二大根因）
mcc 的 `i`、`s` 等循环变量在栈槽：
```
mov %rbx,%r10
mov (%r10),%eax    ; 从栈读 i
add $0x1,%rax      ; i++
... 
mov %eax,(%r10)    ; 存回栈
```
GCC 循环变量全在寄存器。mcc regalloc 在寄存器压力下无差别溢出，
循环体内每次迭代 2–3 次栈访问。

### 热点 C：冗余 mov 风暴（放大因子）
`mov %rax,%r12; mov %r12,%rax` 反复出现（regalloc 复制消除不彻底 +
参数/返回值 slot 化的副产品）。把有效指令数放大 1.5–2 倍。

### 热点 D：无循环优化
- `i*4` 每次重算（无地址递增量，GCC 用 `add $0x4,%rdi`）
- 无 strength reduction、无 IV 消除、无循环不变量提升
- 数组循环退化为"索引重算 + 内存往返"组合

### 热点 E：无 div-by-constant 优化
`x/7` mcc 用除法指令，GCC 用 magic multiply（movabs+imul+sar）。

### 热点 F：函数调用开销大 + 无 TCO
- fib 帧 80 字节（参数/返回值 slot 化 + 5 个 callee-saved push）
- 尾递归 sumn 深度 5M 时 mcc **栈溢出**（GCC -O2 转循环）；改 20K 才能跑
- 无小函数内联（strings 的 my_strlen 等未内联）

## 4. 优化建议清单（按影响排序）

| # | 优化项 | 影响的基准 | 预期收益 | 实现方向 |
|---|--------|-----------|---------|---------|
| 1 | **参数寄存器 hint**：MIR 参数映射到 ABI 参数寄存器（x86_64 rdi/rsi/...，riscv64 a0-a7），函数体直接寄存器访问，去参数 slot 解引用 | 全部 | 大幅（消除每参数内存往返） | regalloc.c 参数 hint（mreg_slots/线性扫描的 ABI 边界倾向）+ func_to_mir 参数处理 |
| 2 | **循环优化 pass**（IndVarSimplify）：地址递增量 + strength reduction + 循环不变量提升 | intloop/fp_mat/strings/sortbench | 中-大幅（循环体瘦身 3-5 倍） | 新 MIR loop pass：识别循环归纳变量，派生指针 IV |
| 3 | **postra 冗余 mov 消除加强** | 全部 | 中（有效指令 -30-50%） | regalloc.c 的 mov-coalescing（复制传播/消除） |
| 4 | **regalloc 栈溢出优先级**：活跃区间长/循环内变量优先寄存器 | 全部 | 中 | 线性扫描的 spill 选择：循环深度权重 |
| 5 | **div/rem-by-constant → multiply-shift** | intloop/mul_acc | 小-中 | mir 优化 pass（magic number 算法） |
| 6 | **小函数内联** | strings/recur | 小-中 | MIR 内联 pass（按函数大小阈值） |
| 7 | **尾调用优化（TCO）** | recur（防栈溢出） | 小（正确性 + 小收益） | MIR 尾递归检测 → 跳转替换 |

**优先级建议**：#1 参数 hint 是最大杠杆（影响所有程序、改动集中、风险低）；
#2 循环优化是长期最大收益；#3/#4 是 regalloc 质量提升；#5-#7 是
专项优化。

## 5. 复现方式

```sh
cd /tmp/bench
export MEUOS_SYSROOT=/tmp/mxx-wt-hazel/projects/sysroot
./compare.sh 3        # mcc vs gcc 全表
objdump -d mcc_intloop_O2 > mcc_int.s && objdump -d gcc_intloop_O2 > gcc_int.s
```
