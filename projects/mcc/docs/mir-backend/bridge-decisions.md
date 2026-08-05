# bridge.c 降级决策清单（MIR → LIR）

> 来源：`src/lir/bridge.c`（557 行）。它是当前 MIR→LIR 唯一翻译点，
> 已经承担了"类型→opcode/宽度"的机器指令选择决策。P1-P3 移植（路线 b）
> 时**直接吸收**本清单，不再重新推导。
>
> 核心结论：LIR opcode 的宽度/符号编码与 MIR 不兼容，bridge 用**显式
> switch**（而非 optab 表）做转换。决策的输入是三元组
> `(MOP op, MType dtype, MType src_type)`，输出是 `(int qop, int cls)`。

## 0. 类映射（mir_to_cls，bridge.c:26）

| MType          | LIR 类 |
|:---------------|:-------|
| MT_I8/I16/I32/PTR | Kw   |
| MT_I64         | Kl     |
| MT_F32         | Ks     |
| MT_F64         | Kd     |
| 其他           | Kx     |

## 1. 直接映射表（mir_to_op，bridge.c:41）— 宽度无关 op

- 算术/位：ADD/SUB/MUL/DIV/UDIV/REM/UREM/NEG/AND/OR/XOR/SHL/SHR/SAR → Oadd…Osar
- 转换：CAST→Ocast，FEXT→Oexts，FTRUNC→Otruncd
- 内存：ALLOCA→Oalloc16，SALLOC→Osalloc
- 其他：COPY→Ocopy，VASTART→Ovastart，VAARG→Ovaarg
- ⚠️ 该表的比较项（CEQ→Oceqw 等）和 SExt/ZExt/F2I/I2F 项会被下面
  的专用分支**覆盖**（宽度是类型相关的，表里只是占位）。

## 2. 宽度/符号决策（按 MOP 分组）

### 2.1 MOP_LOAD（bridge.c:462，宽度=加载字节数）
- dtype → opcode：
  - MT_I8 → Oloadub（**零扩展**）
  - MT_I16 → Oloaduh
  - 其余（i32/i64/f32/f64）→ Oload
- cls 取 **dst->type**（目标值类型），而非 dtype。**有符号字节/半字 load**
  在 func_to_mir 里已拆为 `load + SEXT`，故这里一律零扩展。
- 教训：早期每 load 都用 Oload + cls，`a[0] != '-'`（char load）读了 4 字节。

### 2.2 MOP_STORE（bridge.c:491，宽度=存储字节数）
- dtype → opcode：i8→Ostoreb，i16→Ostoreh，i32→Ostorew，i64→Ostorel，
  f32→Ostores，f64→Ostored
- cls：f32→Ks，f64→Kd，其余 Kw

### 2.3 MOP_F2I / MOP_I2F（bridge.c:360/371，按**源精度/源宽度**选 opcode）
- F2I：源 f64→Odtosi，源 f32→Ostosi；结果 cls=mir_to_cls(dst)
- I2F：4 组合（源宽×目标宽）：
  - i64→f64 = Osltof，i64→f32 = Oultof
  - i32→f64 = Oswtof，i32→f32 = Ouwtof
- 教训：MIR 的 MOP 只含"转换意图"，精度在源/目标类型里，必须显式选。

### 2.4 MOP_SEXT / MOP_ZEXT（bridge.c:391，按**源宽度**选 opcode）
- SEXT：src i8→Oextsb，i16→Oextsh，否则 Oextsw
- ZEXT：src i8→Oextub，i16→Oextuh，否则 Oextuw
- cls：dst i64→Kl，否则 Kw（目标宽度决定 class）
- 教训：源宽度决定 opcode、目标宽度决定 class，两者**必须分开取**。
  曾因只按目标取（i64 常量 256 用 extsb）导致字节扩展吞值。

### 2.5 比较指令（bridge.c:426，MOP 的 dtype 决定宽度）
- 整数：dtype i64 → ...l（Oceql/Ocnel/Ocsltl/Ocslel/Ocsgtl/Ocsgel/
  Ocultl/Oculel/Ocugtl/Ocugel），否则 ...w
- 浮点：dtype f64 → ...d，否则 ...s（CFEQ 等 6 个）
- 结果 cls **强制 Kw/Kl**（i64→Kl，其余 Kw），绝不能用 Ks/Kd——
  否则 ssa.c:96 把结果 retype 成 float，下游 copy.c:354 KBASE assert 炸。

### 2.6 MOP_ALLOCA（bridge.c:414）
- → Oalloc16；常量 size ≤ 0 clamp 到 0（对齐链折叠成负数的场景）。

## 3. 聚合/调用/参数/返回（ABI 入口标记，selcall/selret/selpar 在 target 层做真正降级）

| MIR 结构 | LIR 编码 | 说明 |
|:---------|:---------|:-----|
| MOP_CALL + src[1]=MV_TYPE | Ocall + TYPE(retty_idx) | 聚合返回：selcall 分类（≤16B → RAX:RDX 打包；>16B → 隐藏 sret，结果 Kl=返回垫指针） |
| MOP_CALL 标量 | Ocall + cls=mir_to_cls(dtype) | 标量返回 |
| MOP_ARG + src[0]=MV_TYPE | Oargc + arg[0]=TYPE(idx), arg[1]=源指针 | 聚合实参：selpar 降级（栈拷贝 / 参数寄存器对） |
| MOP_ARG 标量 | Oarg + cls=mir_to_cls(dtype) | 标量实参 |
| MOP_PAR + paramty[k]≥0 | Oparc + TYPE(idx) | 聚合形参：param temp 是 Kl 栈垫指针 |
| MOP_PAR 标量 | Opar + cls=mir_to_cls(type) | 标量形参 |
| MOP_PHI | Phi + cls=mir_to_cls(dtype) | — |
| MOP_RET + retty≥0 | Jretc | 聚合返回（selret 降级） |
| MOP_RET 标量 | Jretw + mir_to_cls(rettype) | Jretw/Jretl/Jrets/Jretd |
| MOP_RET void | Jret0 | — |

## 4. 值与常量编码（valref/refval，bridge.c:100/132）

| MVal/MConst | LIR Ref | 备注 |
|:------------|:--------|:-----|
| MV_TEMP | TMP(lirtmp) | bridge 先遍历 nval 预分配 newtmp，映射存 MVal.lirtmp |
| MV_GLOBAL | CAddr + Sym | hint≠-1 → SExt，否则 SGlo；tls → SThr |
| MV_CONST(MC_INT) | getcon(u.i) | — |
| MV_CONST(MC_FLT) | CBits Con（flt=1/2） | f32→bits.s，f64→bits.d |
| MV_CONST(MC_ADDR) | CAddr + off | — |
| MV_TYPE | TYPE(id) | id 是前端/LIR typ[] 索引 |

## 5. Fn 级初始化约定（bridge.c:184）

- fn->con[0] = 0xdeaddead（未定义），con[1] = 0（CON_Z）
- 物理寄存器占位 tmp[0..Tmp0-1]（fpr 区用 Kd，其余 Kl）
- fn->leaf=1，遇 MOP_CALL 置 0；fn->vararg = mfn->vararg
- fn->slot/salign 初始 0（由 spill/selpar 填充）

## 6. 对 P1-P3 移植的要点

1. P1（机器层）：MIR 的 (op,dtype,src_type)→(opcode,cls) 决策是**平台相关
   起点**，P1 只需把它们变成新后端指令选择器的输入，勿改变语义。
2. P2（ABI）：Ocall/Oargc/Oparc/Jretc 的 TYPE(idx) 编码约定必须保留——
   selpar/selcall/selret 靠它定位 MTypeDesc 分类。
3. P3（isel）：LOAD 零扩展选择、比较结果强制 Kw/Kl、SEXT/ZEXT 源宽/目标宽
   分离，这三个是**踩过坑的教训**，移植时逐条对照测试防回归。
4. MIR 常量池已去重；bridge 转 CBits 的浮点编码（flt 标志）在 emit 层被消费。
