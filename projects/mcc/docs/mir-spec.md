# MIR 规范（MIR Specification）

> MIR = Medium IR，mcc 共享中端 IR，供 C 前端（mcc）与 C++ 前端（m++）共同产出，
> 平台无关，是 MIR/LIR 分离后的中端核心。对应 `.issues/IR-DESIGN.md` 阶段 3。

## 1. 定位

```
C 源 → [C 前端 AST] →┐
                     ├→ MIR（本规范）→ MIR 优化 passes → LIR → target → asm
C++ 源 → [C++ 前端 AST] →┘
```

- MIR 是语言无关三地址码，显式 SSA。
- LIR 是平台相关层，由现有 QBE 后端演进；`src/lir/bridge.c` 是 MIR→LIR 唯一翻译点。

## 2. 类型系统

### 2.1 标量 MType

| MType | 含义 | size | align |
|:------|:-----|:----:|:-----:|
| MT_VOID | void | 1 | 1 |
| MT_I8 | 8-bit int | 1 | 1 |
| MT_I16 | 16-bit int | 2 | 2 |
| MT_I32 | 32-bit int | 4 | 4 |
| MT_I64 | 64-bit int | 8 | 8 |
| MT_F32 | float | 4 | 4 |
| MT_F64 | double | 8 | 8 |
| MT_PTR | 指针 | 8 | 8 |
| MT_AGG | 聚合（struct/union/array） | 见 MTypeDesc | 见 MTypeDesc |

有符号性由 opcode 决定（MOP_DIV 有符号 vs MOP_UDIV 无符号），类型本身不含符号。

### 2.2 聚合 MTypeDesc

- `MField[]`：每字段 `{type, sub, name, offset, bitoff, bits}`。
- `is_array`：`elem_type/elem_desc + nelem`。
- `ext`：C++ 前端扩展槽（vtbl 指针 / RTTI 元数据）。
- 注册：`mfn_addtype(fn, td)` 入函数类型表，返回 id。

## 3. 指令集 MOP

### 3.1 算术/位
MOP_ADD/SUB/MUL/DIV/UDIV/REM/UREM/NEG/AND/OR/XOR/SHL/SHR/SAR

### 3.2 比较（结果 0/1）
整数：CEQ/CNE/CSLT/CSLE/CSGT/CSGE/CULT/CULE/CUGT/CUGE
浮点：CFEQ/CFNE/CFLT/CFLE/CFGT/CFGE

### 3.3 内存
MOP_LOAD / MOP_STORE / MOP_ALLOCA

### 3.4 转换
MOP_SEXT / ZEXT / TRUNC / CAST / F2I / I2F / FEXT / FTRUNC

### 3.5 控制（终结指令）
MOP_JMP / JNZ / RET / CALL

### 3.6 传参
MOP_ARG（call 实参）/ MOP_PAR（函数形参，入口块）/ MOP_VARARG

### 3.7 其他
MOP_PHI / COPY / VASTART / VAARG / SALLOC

### 3.8 扩展点
MOP_EXTRA = 第一个语言相关 opcode；`MIns.extra` 字段选定具体派生指令。

## 4. SSA

- 每值最多一个定义：MIns.dst 或 MPhi.dst。
- 跨块合并仅用显式 MPhi（`MPhi{arg[], blk[]}`，每个 arg 对应一个前驱块）。
- use 链：MVal.use[]（MUse{ins|phi, argn}），供 DCE/GVN/rega 使用。

## 5. 构造 API（src/mir/build.c）

- `mfn_new(name, optlevel)` / `mfn_addblk` / `mblk_new`
- `mval_new(kind, type, td, name)` / `mval_const` / `mval_global` / `mval_type` / `mval_label`
- `mconst_int/flt/addr`（常量池去重）
- `madd(fn, blk, op, dtype, dst, a0, a1)` / `madd0` / `madd1`
- `mterm(fn, blk, op, a0, s1, s2)` / `mret` / `mretvoid`
- `mphi_add(fn, blk, dtype, dst)` 返回 dst；实参通过 MPhi 的 arg[] 填充
- `mfn_addtype(fn, td)` / `mfn_dump(fn, out)` / `mfn_free(fn)`

## 6. 与现有 IR（LIR）的映射

| MIR | 现有 Fn/Ins | 说明 |
|:----|:------------|:-----|
| MVal | Tmp/Ref | valref() 吸收进 MIR 构造 |
| MConst | Con | 常量池 |
| MTypeDesc | typ[] + Typ | emittype() 改填 |
| MOP_xxx | Oxxx | fe_to_mir_op() 转换表 |
| MPhi | b->phi | 显式化 |

## 7. 验证

- `make check-mir-types`：B.1 单测（类型/聚合/值/指令/常量池/dump）。
- 后续：`make check` 全系列 + `bench/run.sh` 性能门禁。

> 状态：✅ B.1 核心 2026-08-01 落地；B.2 优化管线 / B.4 C 迁移待实施。
