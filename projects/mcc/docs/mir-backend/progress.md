# MIR 直接后端进度（progress.md）

> 运维约束：每完成一个可验证的里程碑立即 `git commit + push`，并更新本文档。
> 若因 API/网络中断，从最近的 git 提交 + 本文档续接。

## 概览

- **目标**：MCC 的 MIR 原生后端（MFnM 机器层，替换 LIR bridge 路径），
  新后端代码在 `src/mir/`、`src/target/x86_64/x86_64_m*.c`；参考源
  `src/target/x86_64/x86_64_isel.c` 等**不修改**。
- **开关**：`MCC_MIR_BACKEND=1` 启用新后端（标量函数）；聚合/varargs/TLS
  fallback 到 bridge。`MCC_DEBUG_MBE=1` 输出机器层 dump。
- **Oracle 基线**：`/tmp/mir-backend-base-p2/asm/`（123 测试 bridge 产物，
  P2 时更新；bridge 路径必须字节一致）。
- **P4 设计**：`docs/mir-backend/regalloc-design.md`（isel-debug，A→E 增量）。

## Phase 状态

| Phase | 内容 | 状态 | 提交 |
|-------|------|------|------|
| P0 | bridge 冻结为 oracle（collect.sh + 123 基线入库） | ✅ | 74e9321 |
| P1 | 机器层：MVal/MAddr/MMOP/MFnM，MReg target 化（6 架构） | ✅ | d42efce, 1a5eacd |
| P2 | x86-64 SysV ABI 移植（typclass/selpar/selcall/selret/va） | ✅ | a6c64e7 |
| P3a | MIR 类型系统：func_to_mir 建 MTypeDesc，MV_TYPE 双轨(id+td) | ✅ | 0b07fa8 |
| P3b | 完整 isel + emit：MFnM→x86-64 asm，可运行 | ✅ | eca97b7 |
| P4a | regalloc：活跃区间构造（mreg_intervals） | ✅ | 5e87cce |
| P4b | regalloc：槽分配（mreg_slots，slot4/8 打包） | ✅ | dc0de61 |
| P4c | regalloc：线性扫描（mreg_scan，调用点双池+fixed 占用） | ✅ | 75a5d15 |
| P4d | regalloc：phi 边移动（phi 值强制 spill 避免并行移动覆盖） | ✅ | 7c93bcd |
| P4e | regalloc 接管 emit（寄存器感知 + callee-saved 保存 + 静态 alloca） | ✅ | 2664766 |
| P4fix | Bug 1/2 边界修复（浮点 isel + 栈传参寻址） | ✅ | d6483d5, 3ce88c0 |
| P5a | postra 冗余 mov 消除 + slot4/8 双游标复采 | ✅ | 33385f9, ab12b95, 5581557 |
| P5b | hint 优先级（ABI 边界寄存器倾向） | ✅ | ab12b95 |
| P6a | 聚合函数新后端（去 fallback：BLIT/sret/参数 pad） | ✅ | cc24361（先 2071ae6） |
| P6b | varargs 新后端（去 fallback：ap alloca 32B/帧对齐/常量池标签） | ✅ | f9b9c34 |
| P6d | 切换前验证：123 一致性 0 diff（shift/.globl/sret 修复 daab688） | ✅ | daab688 |
| P6e | 自举验证：mcc_main 崩溃根因已修复（hint 跨 call da6aee4 + calls[64] 去上限 9718e44），self-mcc --version / 编译运行 hello 正常 | ✅ | da6aee4, 9718e44 |
| P6c | 通用修复：slot4/8 无重叠打包 + 帧 rsp 16 对齐 | ✅ | 并入 P6a |
| P6d | 宽度修复：32 位位移/比较（call 返回值高 32 位） | ✅ | 已提交 0612242 |
| P6e | regalloc：hint 跨 call 检查（caller-saved 拒绝） | ✅ | 已提交 da6aee4 |
| P6f | regalloc：calls 数组去 64 上限（超大函数漏 call） | ✅ | 已提交 9718e44 |
| P6g | Bug B 修复：selcall sret 分配 pad（qualtype 24B） | ✅ | 已提交 eccf7ae |
| P6h | Bug C 修复：meuos-libc strtod 不支持 hex float（0x1p63）→ self-mcc 编译 eval.c 崩溃。已修复并全量自举 105/105 | ✅ | 已提交 f412f75 |
| P7 | x86_64 fallback 全部闭环（聚合实参/SALLOC、TLS、动态 alloca/VLA）：MCC_MIR_BACKEND=1 为完整路径，无 bridge fallback，自举通过 | ✅ | 4c908df, 1a1d599, 7e78598 |

## 验证结果（截至 P3b）

- `make check-mir`：mir_test / pass_test / machine_test(86) / mabi_test(43) /
  regalloc_test(44) / bridge 全过。
- `test/c99 + test/c11` 在 `MCC_MIR_BACKEND=1` 下编译 + 运行 **0 失败**。
- 123 测试 bridge 路径 `.s` 与 P2 oracle **0 差异**（硬性回归标准）。
- 新后端运行正确：hello（printf）、fib(10)=55（递归+分支）、gcd/sumsq/浮点/
  extern/complex 均过。

## 边界 bug 修复（isel-debug P3b 独立验证，2026-08-02）

1. **浮点参数读取错误**：emit_addr 的 disp 错放括号内（`(8%rsp)`→`8(%rsp)`）、
   emit_mov 浮点经 xmm0 中转覆盖 ABI 参数寄存器（改直接 movsd，mem→mem 经 r10）、
   func_to_mir 未设 CALL 返回值 MVal.type（regalloc 误分 GPR，isel 补设）。
2. **>6 参数栈传参非法寻址**：selcall 的栈参数空间（SALLOC +stk）未恢复 →
   补 caller 清理 SALLOC -stk；emit 的 SALLOC 直接 subq $n（n 可负）。
3. **浮点指令选择**：map_op 按 dtype 选 FADD/FSUB/FMUL/FDIV/FNEG（isel 层）。

修复验证：fadd/fdiv/fneg 浮点参数、printf 7 参数全过；c99/c11 仍全过；
check-mir 全绿；bridge 路径 0 回归。

## ≤16B 聚合按值返回修复（2026-08-03，bella，Phase 2 验证发现）

### 现象
- `struct Foo{int a,b;} make(void){...}` + `struct Foo f = make()` 在
  MCC_MIR_BACKEND=1 下 SIGSEGV（8/12/16B 全崩；24B+ sret 正常）。
- 覆盖缺口：verify-all.sh 不设 MCC_MIR_BACKEND（当时默认 0）→ cpp 套件
  实际走 bridge；c99/c11/c23 无 ≤16B 聚合按值返回测试。

### 根因（两处，均在 x86_64_mabi.c / x86_64_memit.c）
1. **混合类返回寄存器映射错误**：selret/selcall 用"位置式"映射
   （slot1→XMM1/RDX），而 SysV 用**按类计数**（INTEGER: rax,rdx；
   SSE: xmm0,xmm1）。`{int i; double d}`（{INTEGER,SSE}）的 SSE eightbyte
   应回 XMM0 而非 XMM1（对照 LIR retr()）。
2. **emit LOAD 浮点经 %rax 中转**：MMOP_LOAD 对 F64 目标先 `movq addr,%rax`
   再 `movq %rax,%xmmN`。selret 两个返回块连续 load 时，第二块（F64）的
   load 会把第一块刚放进 %rax 的 INTEGER 块覆盖。

### 修复
- mabi_selret / mabi_selcall 的 ≤16B 分支改为按类寄存器序列
  （retreg[2][2] = {RAX,RDX}/{XMM0,XMM1}，逐 eightbyte 取类计数）。
- emit MMOP_LOAD 增加 F64→XMM 寄存器的直接 `movsd` 路径（不再经 %rax）。
- regalloc 区间起点取"首次 def"（sentinel UINT32_MAX + `min`）：聚合返回
  call 的 dst 会被 mabi_selcall 先指向 pad、再由 call 重定义（multi-def），
  区间必须从最早写入开始，否则线性扫描可能在该值最后一次 def 前把寄存器
  让给邻居。
- 回归测试：test/c11/aggregate_ret_small.c（8/12/16B int、16B SSE、
  16B 混合 {INTEGER,SSE}、嵌套访问）+ test/cpp/aggregate_ret_small.cc
  （链式 operator+ 临时值回传 + 混合 16B 返回）+ test/c11/aggregate_return.c
  与 test/cpp/aggregate_return.cc（含混合 {double,int}→XMM0+RDX、返回值直
  接作实参、24B sret 对照）。

### 验证
- 双路径（默认 bridge + MCC_MIR_BACKEND=1）：check-c-mir fail=0、
  check-cpp-func/neg rc=0；cpp 套件在 MCC_MIR_BACKEND=1 下全绿。
- 覆盖门禁建议：check-cpp 增加 MCC_MIR_BACKEND=1 变体。

### 后续收口（MCC_MIR_BACKEND=1 全量 verify-all 17/17 所需的两个配套修复）
1. **MCC_MIR_BACKEND 目标门控**（emit.c，72a04bd）：MIR-native 后端仅
   x86_64 实现，但调用 mfnm_backend_x86_64 不检查目标，MCC_MIR_BACKEND=1
   会劫持 i386/loongarch64/riscv64/aarch64 编译并输出 x86-64 指令
   （pushq/%rsp）→ 汇编失败。按 `T.name=="x86_64"` 门控，非 x86_64 忽略
   该 env 走 legacy LIR。
2. **MIR-native TLS 适配 PIC**（x86_64_memit.c，16273af）：emit_tls_addr
   一律 local-exec（%fs:0+@tpoff），--shared 下 R_X86_64_TPOFF32 被链接器
   拒绝。T.pic 时改用 initial-exec GOT 形式
   （`movq sym@gottpoff(%rip); addq %fs:0`），对照 legacy Oaddr。
- 最终验证：`MCC_MIR_BACKEND=1 sh test/verify-all.sh` **17/17**（含
  check-driver 的 shared/TLS 回归、check-i386/loongarch64/targets 交叉
  目标）；默认模式 verify-all 亦 17/17，无回归。

## 当前工作点

- **P4 regalloc 全部完成（A-E）**；isel-debug 验证发现的 P3b 遗留 2 个边界
  bug（浮点参数/运算、>6 参数栈传参）已修复并验证（提交 d6483d5 + 3ce88c0）。
- **P5 第一步完成**：`make check-sysroot-static` 自举回归通过（退出 0，
  MIR 新后端改动不影响默认自举链路）。
- **P5 第二步完成**：hello 走 MCC_MIR_BACKEND=1 新后端独立编译运行正确。
- **P5 第三步（postra）完成**：emit_mov 直接 mov（reg↔reg、reg↔mem 不经
  %rax 中转、同寄存器 no-op），add 指令量下降。
- **P5 第四步（hint + slot4/8 复采）完成**：slot4/slot8 双游标恢复
  （i32/f32 用 4 字节槽，emit 对 4 字节槽用 movl/movss），栈帧缩小；
  regalloc 分配优先 MVal.hint 寄存器（机制就位，暂未设置 hint 源）。
- **P5 全部完成**。MIR 新后端独立可用（hello 走 MCC_MIR_BACKEND=1）。
- MCC_MIR_BACKEND=1：标量函数走完整新后端（isel + ABI + regalloc + emit），
  聚合/varargs/TLS/VLA 动态 alloca fallback 到 bridge。
- **P7（2026-08-03，bella）x86_64 fallback 全部闭环**：mbe_supported() 不再
  有剩余 fallback——聚合实参（MOP_ARG/MOP_CALL MV_TYPE）、SALLOC、TLS 全局
  （local-exec `%fs:0 + @tpoff`）、动态 alloca/VLA（MFnM.dynalloc + rsp 调整 +
  epilogue 从 rbp 恢复）全部由 MIR-native 后端处理。验证：
  - check-c-mir 默认与 MCC_MIR_BACKEND=1 双路径 fail=0；
  - check-mir 全绿（mabi_test 45 / regalloc_test 45）；
  - check-sysroot-static 自举默认路径与 MCC_MIR_BACKEND=1 均 exit 0
    （self-mcc 全源码经 MIR-native 编译 + hello 运行正确）。

## Bug 修复记录（isel-debug 验证发现）

- **Bug 1 浮点**：isel 层浮点 ADD/SUB/MUL/DIV/NEG 选浮点 MMOP（FADD..FNEG，
  之前用整数）；emit 的 FNEG 用 0.0-x；MOVSX/MOVZX 从 rax 低位扩展；
  i32 除法从 eax 扩展。验证 fadd=9.0/fdiv=3.5/fneg=-7.0。
- **Bug 2 栈传参**：emit_addr 的 AT&T 格式错误（disp 应在括号外、base 前
  无逗号）导致 `(,%rsp)` 非法寻址；selcall 生成配对的 ±SALLOC（caller
  cleanup），emit 按正负 subq/addq。验证 printf 7 参数正确。

## 关键决策/发现

- 机器层 MInsM 直接持有 MVal*（src/dst/addr），无反向 use 链；
  regalloc 区间从指令扫描构造（非设计文档假设的 MUse 链）。
- 机器层无 MPhi（P3b 已把 SSA phi 降级为 pred 块尾 MMOP_MOV copy），
  phi 值经 `extra=1` 标记强制 spill（P4d，避免并行移动覆盖）。
- emit 的 scratch 寄存器（rax/rcx/rdx/r9/r10/r11/xmm0）从 regalloc 池排除
  （MTargetM.scratch），累加器不与操作数别名。
- 线性扫描的回边缺口（循环头入口值被循环体临时值覆盖）经"区间跨回边
  延伸"修复；`fm->regsused` 记录全部已分配寄存器（非当前活跃集）。
- 所有 spill 槽 8 字节对齐（emit 用 movq 访问），4 字节值不越界。
- 静态 alloca 用帧内 leaq 预留（不碰 %rsp），动态 alloca（VLA）fallback。

## 独立验证记录（isel-debug，干净 worktree 全量复测）

> 每轮均在**干净 worktree**（`git worktree add` 到已提交状态）构建验证，
> 不混入工作区未提交改动。硬性标准：c99/c11 新后端 0 失败；123 测试
> bridge 路径 .s 与 oracle 字节一致；代表性程序运行正确。

### 轮 1 — P3b（eca97b7，2026-08-02）

- c99/c11 34 测试 `MCC_MIR_BACKEND=1` 编译全通过；19 个有 main 测试
  MIR 后端 vs bridge 退出码+stdout **19/19 一致**（确认真走新后端，均含
  .bb 块结构）。
- hello（printf）、fib(10)=55 运行正确；39 个 C 测试 bridge .s 与 oracle
  **39/39 字节一致**。
- 聚合/varargs fallback 混合模式不崩，汇编特征确认标量走 MIR、聚合走
  bridge。
- **发现 2 个 P3b 边界 bug**（已由 mir-backend 修复，见上方"边界 bug
  修复"节）：
  1. 浮点参数读取错误：`fadd(7,2)` 返回 0.0（bridge 3.5）——MIR 后端
     prologue 参数 slot 偏移错乱 + 浮点用整数指令（addq 而非 addsd）。
  2. >6 参数栈传参非法寻址：`printf` 7 参数生成 `movl %eax,(,%rsp)`
     （rsp 作 index 非法）。

### 轮 2 — d6483d5（Bug 1/2 修复 + P4 提交，2026-08-02）

- **发现阻断性构建缺陷**：`x86_64_memit.c:637/784 'g_salloc' undeclared`
  ——g_salloc 被 d6483d5 新增的 MMOP_SALLOC 分支使用但**从未定义**，干净
  worktree 无法构建。验证时加本地补丁 `static int g_salloc;` 绕过。
- Bug 1/2 复测通过：fadd(4,5)=9.0、fdiv(7,2)=3.5、fneg(7)=-7.0、printf
  7/9 参数全对。
- **发现 P4 参数传递段错误**：`int x=add(3,4); printf("%d",x)` GP fault
  （libc 内）；simple/mul3/big/gcd+sumsq 组合**全部段错误**。根因：被调方
  bb1 用 rsi/r8 解引用参数 slot，但 slot 指针语义跨块未保持，且调用方传值
  （rdi/rsi=3,4）与被调方按地址解引用不匹配 → `movl (%rsi)` 把参数值当地址。
- P4 regalloc 抽查：simple 汇编仍全栈槽解引用（`movl (%r10)`），未真正
  用寄存器——当时判定"P4 未实现寄存器分配"（后经 f9b9c34 轮**修正**，
  见下）。

### 轮 3 — f9b9c34（P5/P6 后，2026-08-02）

- **独立构建 ✅**：无 g_salloc 错误（P5/P6 重构移除该变量）。
- **参数传递段错误已修复 ✅**：simple=7、mul3=15、big=7、gcd=12、
  sumsq=385 全部正确，与 bridge 一致。
- **regalloc 真实性确认 ✅（修正轮 2 误判）**：`MCC_DEBUG_MBE=1` dump 显示
  simple/compute 全部中间值分配物理寄存器（reg=3/4/5，slots 全 -1，
  regsused=0x38）；emit 有 `v->reg >= 0 → %reg` 分发（memit.c:212）。
  此前误判为"全栈槽"的 `movl (%r10)` 实为 ABI 层把 SysV 参数（rdi/rsi）
  落栈后的 load 指令（`@[%v3]` 地址），加载结果确在寄存器中运算。
- **c99/c11 0 失败 ✅**：34 编译全过，19 个有 main 测试 MIR 新后端 vs
  bridge **19/19 一致**。
- **bridge 基线一致 ✅**：39 个 C 测试 .s 与 oracle **39/39 字节一致**
  （84 cpp 为 m++ C++ 测试，不在 mcc 范围）。
- hello + fib(10)=55 + varargs `sum_va(4,1,2,3,4)=10` 全部正确（P6b
  varargs 新后端支持确认）。

### 遗留（非正确性）

- **isel 冗余 store/load**：中间值被 isel 层 store 到栈再 load（尽管
  regalloc 已分配寄存器），如 `x=3` 存 -16(%rbp) 后立即重载。功能正确、
  性能未优化；属 isel 缺少 copy-propagation/load-elimination，不影响
  P4/P5 可靠性结论，后续可作为优化项。

## 未初始化读排查 + calls 审计（isel-debug，2026-08-02）

### calls[64] 修复完备性审计（9718e44 之后）
- **calls 动态 realloc 已修复 mcc_main 崩溃**（mcc_main 241 个 call 远超
  原 64 上限，超限 call 被丢弃 → 跨 call 区间误判不跨 → 分到 caller-saved
  → call clobber）。self-mcc `--version` 稳定（干净 worktree 实测）。
- 其余固定数组审计：
  - `fixed[64]`/`busy[64]`：按物理寄存器索引，pos/bit 动态正确。
  - `act[256]`：同时活跃区间上限，nact 满 256 时**强制 spill**（非崩溃，
    性能隐患，超大函数可能 spill 过多）。
  - `cand=malloc(nval)`（未清零）：只读前 ncand 个（已赋值），无泄漏。
  - MVal 全 calloc 初始化（reg=-1/slot=-1/hint=-1），regalloc/spill/emit
    路径未发现明确的未初始化读。

### "不稳定崩溃"实测：是确定性 Bug B，不是未初始化读
- 干净 worktree（9718e44）构建 self-mcc 编译最简 `int main(void){return 0;}`：
  --specs=host **20/20 崩**、默认 specs **10/10 崩**（100% 确定性）。
- 崩溃固定：`segfault at 0 ip 0x4a58d1 error 6`（declspecs 写 NULL）。

### Bug B 根因：isel 错误消除空指针检查
- declspecs 源码（src/parse/specs.c:299-302）：
  ```c
  if (sc) *sc = SCNONE;   if (fs) *fs = FUNCNONE;   if (align) *align = 0;
  ```
- **bridge 版保留空检查**（`cmpq $0,%rdx; jz`），**MIR 后端版无条件
  `movl $0,(%rdx)` 写 NULL** → 崩溃。
- 最小复现 `void set(int *p){ if(p) *p=0; }` MIR 后端**正确**（保留空检查），
  说明是 declspecs 大函数的特定控制流触发 isel/opt 错误折叠条件 store。
- 建议方向：检查 MIR fold/gvn 对 `if(ptr) *ptr=const` 的处理（对比
  MCC_DEBUG_MBE 的 pre/post-pass MIR 定位空检查消失的 pass）。

**结论**：P4 的两个阻断性缺陷（g_salloc 未定义、参数传递段错误）在
P5/P6 已彻底修复；regalloc（线性扫描，依据 regalloc-design.md）真实
分配寄存器；P4/P5 可靠，默认切换（MCC_MIR_BACKEND 接管）可评估。

## Bug C 修复记录（P6h，2026-08-02）

### 现象
- self-mcc（MIR 编译的静态链接产物）编译 `src/sema/eval.c` 崩溃，
  MIR dump 中断于 `$c336(i64)113` 附近（mark_use 阶段误报为崩溃点）。
- host mcc 编译同一文件成功；self-mcc 报
  `invalid floating constant suffix 'x1p63'`。

### 根因
- self-mcc 静态链接 **meuos-libc**（`--sysroot=... --nostdlib --static`），
  其 `strtod`（src/stdlib/convert.c）不支持 C99 hex float（6.4.4.2）。
- eval.c 含 `0x1p63`/`0x1p64` 字面量：meuos-libc strtod 解析 `0` 后把
  `x1p63` 留给 expr_primary.c 判定为非法后缀 → 编译失败。
- host mcc 动态链接 glibc，glibc strtod 支持 hex float → 正常。
- "mark_use 带参函数崩溃" 是误诊：崩溃是 eval.c 编译失败的连锁表现。

### 修复（f412f75）
- meuos-libc strtod 增加 C99 hex float 解析（parse_hex_float：mantissa
  精确二进制累积 + p 指数 2^exp 缩放）+ 纯 hex 整数回退（`0x10` → 16）。
- 十进制路径不变；与 glibc 对照验证（0x1p63/0x1.8p3/0x1p-2/-0x1p4 全等）。

### 验证
- self-mcc 编译 eval.c（含 hex float）exit=0，产物与 host 相同（5416B）。
- self-mcc 全量编译 mcc 自身源码 **105/105 通过**（此前 104/105，唯一失败 eval.c）。
- hello 经 self-mcc 编译运行返回正确值。

### 教训
- 自举产物的 libc 依赖与 host 不同：静态链接 meuos-libc 的程序会暴露
  libc 缺失特性，而 host 动态链接 glibc 掩盖。自举回归是 libc 完备性的
  第一道防线。

