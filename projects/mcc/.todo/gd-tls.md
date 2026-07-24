<!--
priority: P1
status: in_progress
note: 5 个跨架构 GD-TLS 缺口的分阶段实施计划;Phase A 是 Phase B/C 的前置;Gap 5 受 P6 动态链接阻塞
start_ts: 2026-07-24
-->

# 待实现：跨架构 GD-TLS (General-Dynamic TLS)

> ## ⚠️ 任务断点 (2026-07-24, 留给后续模型继续)
>
> **当前 WIP 状态**:已落地的代码改动(未提交到 main,在 feat/gd-tls-ocall 分支):
>
> - `mcc/src/irgen/emit.c` `valref()` VALUE_GLOBAL 分支(行 198-221):
>   在 `extern _Thread_local` + `-fPIC` 时 emit `Oarg`(SGenThr 描述符) +
>   `Ocall`(`__tls_get_addr`) + `fn->leaf=0`。**当前会触发 SSA 错误**。
> - `mcc/src/target/{x86_64,aarch64,riscv64,loongarch64}/*_emit.c` `loadaddr()`:
>   新增 `case SGenThr`,生成各架构 GD 描述符重定位(`@tlsgd`/`:tlsgd:`/
>   `la.tls.gd`/`%gd_pc_*`),**不再内联 call**(由 IR 的 Ocall 负责)。
> - `meuos-libc/src/thread/__tls_get_addr.c`:已加 4 架构(x86_64/aarch64/
>   riscv64/loongarch64)实现,通过读 thread pointer + ti_offset 返回地址。
>
> **阻塞问题(必须修复)**:
>
> `valref()` 是纯翻译函数(`Translate a frontend struct value * to a IR Ref`,
> emit.c 行 152),**不应该 `*curi++` emit 指令**。当前 GD 分支在 valref 内部
> emit Oarg/Ocall,触发:
>
> ```
> mcc: ssa temporary %tlsgd.1 is used undefined in @body
> ```
>
> 复现命令:`./mcc --target=x86_64 -fPIC -S -o /tmp/x.s /tmp/gd_tls_test.c`
> 测试用例:`extern _Thread_local int gd_var; int *get_gd(void){return &gd_var;}`
>
> 根因分析:`valref()` 在 emitfunc 主循环(行 451-608)的多个位置被调用
> (行 477/481/492/497-498/526-527/539/557/564/603-605/629/640),通常作为
> `*curi++ = (Ins){..., .arg={valref(...), ...}}` 的参数被求值。C 求值顺序
> 下,valref 内部 emit 的 Oarg/Ocall 会先写入 curi,外层指令后写入,顺序本身
> 正确。但 SSA 检查报告 result(`%tlsgd.1`)在 `@body` 块被使用但"未定义"。
> 怀疑 valref 在某些调用点(如行 629 `qb->jmp.arg = valref(...)`)被调用时,
> emit 的 Ocall 落到了与前一个块共享的 insb 缓冲,导致 def-use 链跨块断裂。
>
> **后续模型可选方案(推荐顺序)**:
>
> - **方案 B(推荐)**:把 GD-TLS 的 Oarg/Ocall 生成移出 valref,放到 stmt 级别。
>   在 frontend(`src/irgen/` 或 `src/sema/`)处理 `&extern_thread_local` 表达式
>   时,识别 GD 场景,生成独立的中间指令序列(desc = addr SGenThr; result =
>   call \_\_tls_get_addr(desc)),把 result 作为表达式结果。参考 `case ICALL`
>   (emit.c 行 526-583)的 Oarg/Ocall 处理模式。
> - **方案 C**:新增 IR 操作码 `Ogenthr`(类似 `Oaddr` 但语义为"GD TLS 取地址"),
>   在 irgen 层生成,emit 层 lowering。改动面较大但语义最干净。
> - **方案 D**:在 `case Oaddr`(emit.c 默认分支)中识别 a0 是 SGenThr 常量,
>   就地生成 Oarg/Ocall 替换 Oaddr。改动最小但破坏 Oaddr 的纯地址语义。
>
> **已就绪无需重做**:
>
> - `SGenThr = 4` 枚举(`include/ir.h`)及各 emit/ir_util/printfn 的断言宽松
> - 4 架构 emitter 的 `case SGenThr` 描述符地址生成
> - libc 4 架构 `__tls_get_addr` 运行时
>
> **验证命令**:
>
> ```bash
> cd projects/mcc && make -j4
> # 回归(LE/IE 路径,必须仍通过):
> timeout 60 bash test/driver/feature-regress.sh
> # GD 端到端(修复后应该通过):
> cat > /tmp/gd_tls_test.c <<EOF
> extern _Thread_local int gd_var;
> int *get_gd(void) { return &gd_var; }
> EOF
> for a in x86_64 aarch64 riscv64 loongarch64; do
>   ./mcc --target=$a -fPIC -S -o /tmp/gd_$a.s /tmp/gd_tls_test.c
>   grep -E '__tls_get_addr|tlsgd|tls_gd|gd_pc' /tmp/gd_$a.s
> done
> ```
>
> **不要回退**已落地的 emitter SGenThr case 和 libc 实现 —— 它们是正确的,
> 只缺 irgen 层的 Ocall 生成机制重构。

> 日期：2026-07-23
>
> 当前各架构仅有 LE (Local-Exec) 和 IE (Initial-Exec) 实现，GD (General-Dynamic)
> 和 LD (Local-Dynamic) 均未支持。本文拆解 5 个独立缺口，明确 P6 动态链接阻塞
> 关系和分阶段实施路径。

---

## 1. 背景

### 1.1 各架构 TLS 现状

| 架构            |              LE              |          IE          | GD  | LD  | 备注                                |
| --------------- | :--------------------------: | :------------------: | :-: | :-: | ----------------------------------- |
| **x86_64**      |    ✅ `%fs:0` + `@tpoff`     | ✅ `@gottpoff(%rip)` | ❌  | ❌  | 最完整，`--shared` 时自动降级 IE    |
| **aarch64**     | ✅ `tpidr_el0` + `:tprel_*:` |          ❌          | ❌  | ❌  | Linux 无 IE；macOS 独占 `@tlvppage` |
| **riscv64**     |        ✅ `%tprel_*`         |    ❌ `die(...)`     | ❌  | ❌  | IE emit 显式 die                    |
| **loongarch64** |      ✅ `TP` + `%le_*`       |    ❌ `die(...)`     | ❌  | ❌  | IE emit 显式 die                    |
| **i386**        |     ✅ `%gs:...@ntpoff`      |          ❌          | ❌  | ❌  | TLS 整体未端到端验证                |

### 1.2 GD-TLS 是什么

GD-TLS 是 DSO 中访问外部 `_Thread_local` 变量的唯一可移植方式：
编译器生成调用 `__tls_get_addr` 的指令序列，链接器填入 `DTPMOD`/`DTPOFF`
重定位，动态链接器在加载时解析模块 ID 和偏移。

### 1.3 五个独立缺口

GD-TLS 端到端需要以下 5 个组件：

| #   | 缺口                                  | 受 P6 阻塞 | 预计工作量 |
| --- | ------------------------------------- | :--------: | :--------: |
| 1   | mcc IR 符号类型定义（新增 `SGenThr`） |   ❌ 否    |     小     |
| 2   | mcc emit GD 指令序列（x86_64 优先）   |   ❌ 否    |     中     |
| 3   | mcc TLS 模型选择逻辑（irgen 策略）    |  ⚠️ 部分   |     中     |
| 4   | meuos-libc `__tls_get_addr` 运行时    |   ❌ 否    |     中     |
| 5   | meuos-toolchain 链接器 GD 重定位      |   ✅ 是    |     大     |

---

## 2. 缺口详细描述

### 2.1 缺口 1：mcc IR 符号类型定义

**文件**：`include/ir.h`

**当前定义**：

```c
enum {
    SGlo     = 0, /* direct access */
    SThr     = 1, /* local-exec TLS */
    SExt     = 2, /* GOT/PLT access */
    SExtThr  = SExt|SThr, /* =3, initial-exec TLS */
} type;
```

**需新增**：

```c
SGenThr  = 4,  /* general-dynamic TLS */
```

**注意**：`SExtThr = SExt|SThr = 3`，因此 `SGenThr` 使用独立值 4，不与 `SExt` 位组合。

**需同时审计的断言放宽**（多处 `assert((con->sym.type & ~SExt) == SGlo)` 需兼容 `SGenThr`）：

- `x86_64_emit.c` (L202)
- `loongarch64_emit.c` (L58)
- `riscv64_emit.c` (L114)
- `ir_util.c` 中 `addcon()` (L509-513)
- `printfn.c` (L1-4)
- `fold.c`、`alias.c`

### 2.2 缺口 2：mcc emit GD 指令序列

每个后端的 `loadaddr()`（`Oaddr` case）需新增 `SGenThr` case。

**x86_64 标准 GD 序列**（SysV ABI §3.5.3）：

```asm
leaq    sym@tlsgd(%rip), %rdi
call    __tls_get_addr@plt
; %rax = TLS 块中变量的地址
```

**关键设计决策**：

- `Oaddr` 当前是无副作用的地址计算（lea），但 GD 序列包含 `call`（有 caller-save 寄存器副作用）
- 方案 A：在 `Oaddr` case 中直接内联生成 call 序列
- 方案 B：新增独立 IR 操作码 `OgenThr`（更干净，但影响 IR 层）
- 建议采用方案 A，与 QBE 的 `loadaddr()` 模式一致

**其他架构 GD 序列参考**（SysV ABI 各架构章节）：

- **aarch64**：`adrp xn, :tlsgd:12:` + `add x0, xn, :tlsgd_lo12:` + `bl __tls_get_addr`
- **riscv64**：`lui a0, %tls_gd(v)` + `addi a0, %tls_gd(v)` + `jal __tls_get_addr`
- **loongarch64**：`pcalau12i $a0, %gd_pc_hi20(sym)` + `addi.d $a0, $a0, %gd_pc_lo12(sym)` + `bl __tls_get_addr`
- **i386**：`leal sym@tlsgd(,%ebx,1), %eax` + `call ___tls_get_addr@plt`（注意 i386 是 `___tls_get_addr`，三个下划线）

### 2.3 缺口 3：mcc TLS 模型选择逻辑

**文件**：`src/irgen/expr.c`、`src/sema/targ.c`

**需求**：

- 当编译 `-fPIC` 或 `--shared` 时，外部 `_Thread_local` 变量应使用 GD（缺省）或 IE（如果已知为 initial-exec）
- 当前 mcc 所有 TLS 访问强制使用 LE（`SThr`），导致 `--shared` 下无法链接外部 TLS

**策略建议**：

- 在 `targ.c` 的 `loadtls()` 或等价函数中新增 `-ftls-model=` 参数处理
- 默认：`-fPIC` 时外部 TLS → GD，本地 TLS → LE
- `-fno-plt` 或 `-ftls-model=initial-exec` 时 → IE

### 2.4 缺口 4：meuos-libc `__tls_get_addr` 运行时

**文件**：`src/arch/x86_64/tls.c` （或新增 `src/thread/__tls_get_addr.c`）

**musl 参考实现**（`src/thread/__tls_get_addr.c`）：

```c
typedef struct {
    unsigned long ti_module;
    unsigned long ti_offset;
} tls_index;

void *__tls_get_addr(tls_index *ti)
{
    // 1. 从 ti->ti_module 获取 DTV (Dynamic Thread Vector)
    // 2. 若 mod->gen != dtv[mod->index].gen，需要重定位
    // 3. 返回 dtv[mod->index].base + ti->ti_offset
    //
    // 简化版（无 dlopen 时，静态链接下直接返回内存地址）：
    //   return (void*)(__pthread_self()->dtv[ti->ti_module] + ti->ti_offset);
}
```

**依赖**：需要 `struct __pthread` 中包含 DTV 指针数组。当前 `tls.c` 中 `struct __pthread` 仅包含 `dtv[1]`（单元素数组占位符），需扩展为动态 DTV。

### 2.5 缺口 5：meuos-toolchain 链接器 GD 重定位

**完全被 P6 动态链接阻塞**。

所需链接器支持的重定位类型：

- x86_64：`R_X86_64_DTPMOD64`、`R_X86_64_DTPOFF64`、`R_X86_64_TPOFF64`
- aarch64：`R_AARCH64_TLSDESC_*`、`R_AARCH64_TLSGD_*`
- riscv64/loongarch64：`R_RISCV_TLS_DTPMOD*`、`R_LOONGARCH_TLS_DTPMOD*`

当前 `mt/ld` 尚未支持 `ET_DYN` 输出、PLT/GOT 重定位、动态段生成。在这些基础
能力就绪前，GD 重定位无法落地。

---

## 3. 前置依赖链

```
mcc IR 符号类型 (缺口1)     ← 无阻塞
     ↓
mcc emit GD 序列 (缺口2)    ← 无阻塞，可独立开发
     ↓
__tls_get_addr 运行时 (缺口4) ← 无阻塞，可独立开发
     ↓
TLS 模型选择 (缺口3)        ← 依赖 -fPIC 编译可用
     ↓
P6 动态链接器就绪
    ├── mt/ld: ET_DYN 输出
    ├── mt/ld: PLT/GOT 重定位
    └── ld.so: 动态加载器本身
     ↓
缺口5 链接器 GD 重定位       ← 完全阻塞
     ↓
端到端验收：DSO 加载 GD-TLS 变量
```

---

## 4. 分阶段实施建议

### Phase A（不受 P6 阻塞，可立即开始）

| 任务                                  | 文件                                    | 优先级 |
| ------------------------------------- | --------------------------------------- | :----: |
| IR: 新增 `SGenThr = 4` 枚举           | `include/ir.h`                          |   P0   |
| IR: 宽松断言                          | 各 emit + `ir_util.c` + `printfn.c`     |   P0   |
| libc: 实现 `__tls_get_addr`（简化版） | `src/thread/__tls_get_addr.c` + `tls.c` |   P1   |
| emit: x86_64 GD 指令序列              | `x86_64_emit.c` `Oaddr` case            |   P1   |

### Phase B（待 -fPIC 和 P6 部分就绪）

| 任务                      | 文件                 | 优先级 |
| ------------------------- | -------------------- | :----: |
| emit: aarch64 GD 序列     | `aarch64_emit.c`     |   P2   |
| emit: riscv64 GD 序列     | `riscv64_emit.c`     |   P2   |
| emit: loongarch64 GD 序列 | `loongarch64_emit.c` |   P2   |
| emit: i386 GD 序列        | `i386_emit.c`        |   P2   |
| irgen: TLS 模型选择策略   | `expr.c` / `targ.c`  |   P2   |

### Phase C（P6 完整就绪后）

| 任务                     | 文件                  | 优先级  |
| ------------------------ | --------------------- | :-----: |
| ld: ET_DYN 输出          | `meuos-toolchain/ld/` | P0 (P6) |
| ld: PLT/GOT 重定位       | `meuos-toolchain/ld/` | P0 (P6) |
| ld.so: 动态加载器        | `meuos-libc/ld.so/`   | P0 (P6) |
| ld: GD 重定位类型        | `meuos-toolchain/ld/` |   P1    |
| 端到端测试: DSO TLS 访问 | `test/tls/`           |   P1    |

---

## 5. 关联文件清单

### mcc 侧

| 文件                                        | 修改内容                 |
| ------------------------------------------- | ------------------------ |
| `mcc/include/ir.h`                          | 新增 `SGenThr=4` 枚举    |
| `mcc/src/ir/printfn.c`                      | 新增 `SGenThr` 打印 case |
| `mcc/src/ir/ir_util.c`                      | `addcon()` 宽松断言      |
| `mcc/src/opt/fold.c`                        | 宽松 TLS 符号类型断言    |
| `mcc/src/opt/alias.c`                       | 宽松 TLS 符号类型断言    |
| `mcc/target/x86_64/x86_64_emit.c`           | GD 指令序列 + 断言宽松   |
| `mcc/target/aarch64/aarch64_emit.c`         | GD 序列（Phase B）       |
| `mcc/target/riscv64/riscv64_emit.c`         | GD 序列（Phase B）       |
| `mcc/target/loongarch64/loongarch64_emit.c` | GD 序列（Phase B）       |
| `mcc/target/i386/i386_emit.c`               | GD 序列（Phase B）       |
| `mcc/src/irgen/expr.c`                      | TLS 模型选择             |
| `mcc/src/sema/targ.c`                       | TLS 模型配置参数         |

### meuos-libc 侧

| 文件                                     | 修改内容         |
| ---------------------------------------- | ---------------- |
| `meuos-libc/src/thread/__tls_get_addr.c` | 新增运行时       |
| `meuos-libc/src/internal/pthread_impl.h` | DTV 结构定义扩展 |

### meuos-toolchain 侧（Phase C / P6）

| 文件                  | 修改内容                |
| --------------------- | ----------------------- |
| `meuos-toolchain/ld/` | ET_DYN 输出             |
| `meuos-toolchain/ld/` | PLT/GOT 重定位          |
| `meuos-toolchain/ld/` | GD DTPMOD/DTPOFF 重定位 |

## 验收标准

```bash
# Phase A: verify SGenThr enum added to IR
grep -q 'SGenThr.*=.*4' projects/mcc/include/ir.h
# Phase A: verify x86_64 GD sequence emits __tls_get_addr
cat > /tmp/test_gd_tls.c << 'EOF'
extern _Thread_local int gd_var;
int *get_gd(void) { return &gd_var; }
EOF
cd projects/mcc && ./mcc --target=x86_64 -S -o /tmp/test_gd_tls.s /tmp/test_gd_tls.c 2>/dev/null
grep -q '__tls_get_addr' /tmp/test_gd_tls.s && echo "GD-TLS emit: PASS"
# Regression gate: existing LE/IE TLS tests must still pass
cd projects/mcc && make check
```
