---
name: mcc-toolchain P0 缺陷闭环进度（2026-08-04）
description: i386 PIC GOT 与 ld.so 动态链接器 P0 闭环的实施与验收基线
type: project
---

# mcc-toolchain P0 缺陷闭环（2026-08-04）

## 工作流基线（c14c7a2）

- 起点：mcc PASS=19，toolchain `make check` 14 项门禁全 PASS。
- 真实缺口：i386 `-fPIC` 不生成 GOT/PLT 序列（check-pic-verify FAIL）；
  ld.so 的 `rtld_load_needed` 不可达、PIE `.rela.dyn` 缺失、`/lib/ld-meuos.so.1` 未安装、零门禁。

## 已完成

### 主分支 tmp/lead-doc-mir-baseline（HEAD df962a0）

| 提交 | 组件 | 描述 |
|------|------|------|
| 96f7642 | mcc | docs: drop empty src/lir from BE_DIRS in Makefile |
| db4784a | mt | fix check target deps and arch detection（check-as-libc-x86_64 补规则 + check-ld-x86_64 加 $(MCC) prereq） |
| c8e9d1f | mcc | docs: describe single MIR-backend pipeline |
| c46d06a | mcc | docs: simplify mir_matrix.sh to single-path MIR regression |
| e00cbf1 | mcc | docs: fix _mabe.c typo to _mabi.c |
| df962a0 | mt | tighten arm ELF architecture check（arm, *eabi 防 AArch64 误判） |

### 独立分支 tmp/documenter/mir-comment-cleanup

| 提交 | 组件 | 描述 |
|------|------|------|
| 29f96e6 | mcc | docs: drop stale LIR-bridge/dual-backend comments in src |
| 430d3fc | mcc | docs: sync verify-all.sh comments to MIR single-path |

### 独立分支 tmp/i386-pic-got（i386 PIC GOT 实施）

| 提交 | 组件 | 描述 |
|------|------|------|
| abb0131 | mcc | i386: declare g_pic and reserve EBX for PIC GOT（machine.c .reserved=EBX；i386_memit.c extern g_pic） |
| 7e1d63d | mcc | i386: emit PIC prologue/epilogue and save EBX（call __x86.get_pc_thunk.bx + addl $_GLOBAL_OFFSET_TABLE_,%ebx） |
| e5a31be | mcc | i386: route external globals through GOT under PIC（@GOT(%ebx) + @plt） |
| 24e73a8 | mcc | i386: emit TLS initial-exec under PIC（%gs:0 + @gotntpoff） |
| c3953ea | mcc | mir: thread g_pic into mreg_pool_build for i386 EBX（W1：EBX 仅在 PIC 时排除 regalloc） |
| 98cd85a | mcc | i386: drop dead emit_mval（S1：i386_memit.c 死代码清理） |

验证：GNU as + readelf -r 100% 达标，R_386_GOT32X/PLT32/GOTPC/TLS_GOTIE 全部正确；
make check-pic-verify 通过，非 PIC/其它架构无回归。
非 PIC 高寄存器压力函数出现 14 次 `movl %ebx` 临时分配（修复前为 0）→ W1 生效；
PIC EBX 零临时分配 → 仍作 GOT base。mt 侧 i386 GOT 重定位留待 P1。

## 进行中（2026-08-04 截止）

- ~~tmp/rtld-p0：rtld-executor 实施 ld.so P0 4 切片~~（已完成，见下）
- ~~i386-pic-review：default 变体独立审阅 tmp/i386-pic-got~~（已完成）

### 独立分支 tmp/rtld-p0（ld.so P0 闭环，已完成推送）

| 提交 | 组件 | 描述 |
|------|------|------|
| 153be27 | mt | rtld: fix DT_NEEDED dead code, main_lib->base, and DT_PLTRELSZ |
| 3f2c354 | mt | ld: fix PIE .rela.dyn generation and add -dynamic-linker（含 build_rela_dyn 条件 + have_rela 扩到 (shared||pie) + ensure_pie_section 用 ctx->dynamic_linker） |
| 82a4b40 | mt | rtld: resolve SHN_UNDEF symbols via rtld_find_sym |
| f88ae83 | mt | install ld.so and add check-rtld e2e gate |

验证：真实 PIE（`global@gotpcrel`，`.int 42`）经 `mt/ld -pie -dynamic-linker` 链接 + 内核 PT_INTERP 加载修复后 ld.so → 运行时退出码 42；
`make check` 含 check-rtld 全 PASS。install 将 ld.so 放到 $(DESTDIR)/lib/ld-meuos.so.1（与 .interp 硬编码 /lib/ld-meuos.so.1 严格一致）。

**专员制团队已就位**：exec-mcc(default/MiniMax-M3)、exec-toolchain(原 rtld-executor)、doc-pm(lite)、reviewer-auditor(lite)；product-manager(reasoning) 按需 spawn。

## Why

- 必须为后续 m++ 阶段与端到端动态链接通过扫清真 P0；
- 与首批基线修复（df962a0）保持分支独立，避免并发 build 目录践踏与不可回滚大提交。

## How to apply

- 续接 m++ 或动态链接 P1 时，从 tmp/lead-doc-mir-baseline + tmp/i386-pic-got + tmp/rtld-p0 三分支并行推进；
- 跨域修改需新建 worktree（`worktree-<name>` 或 `tmp/<agent>/<feature>`）；
- 任何 mcc 端 PIC 改动先 GNU as 验证，再评估是否需要 mt 侧 GOT 重定位。

## P2 修复记录（2026-08-04）

### mt/readelf PIE `.dynamic` 解析修复

- **commit**：`49349b2`（分支 `tmp/exec-toolchain/mt-readelf-pie`）
- 根因**不在 readelf 端**，而在 **mt/ld 端**：`src/ld/elfwriter.c` 生成的 `.dynamic` 节区为 `sh_type=PROGBITS` 且 `sh_link=0`，不符合 ELF 规范（规范应为 `SHT_DYNAMIC`、`sh_link`→`.dynstr`）。
- readelf 端修复：`dump_dynamic` 优先 `PT_DYNAMIC` phdr、对 `sh_type=PROGBITS` 回退兼容、strtab 按名查 `.dynstr`（不依赖 sh_link）。已关闭待办 `.todo/meuos-toolchain/mt-readelf-pie-dynamic.md`。

### 新增 P2 待办：mt/ld `.dynamic` 节区规范化

- 新待办：`.todo/meuos-toolchain/mt-ld-dynamic-section.md`
- 目标：mt/ld 生成的 `.dynamic` 节区符合 ELF 规范——`sh_type=SHT_DYNAMIC`、`sh_link`→`.dynstr`，从根源消除 readelf/外部工具对 PROGBITS+sh_link=0 的兼容负担。
- **已闭环**（commit `0015271e`，`src/ld/link.c` L3830/L2839 两处 diff）；待办已关闭。

### P2 代码注释 / 归属建议（待扫尾）

- **regalloc.c:26 过时注释**：注释写 "from the driver (main.c)"，但实际全局 `g_pic` 归属 `src/mir/machine.c`（MIR 层强定义），注释与现状不符，建议后续清理。
- **check-mir 手工清单与 MEMIT_SRCS 归属文档化**：建议在 Makefile / ARCHITECTURE 注明"**MIR 层被单测链接清单强引用的全局必须定义在 `src/mir/machine.c`**"（check-mir-* 手工清单含 machine.c，不含 main.c/memit.c），避免后续误放到 target memit.c。

## P2 后续任务（2026-08-04）

### rtld e2e 端到端实跑验证（需合并 rtld-p0）

- 新待办：`.todo/meuos-toolchain/rtld-e2e-pie-verify.md`
- mt/ld `.dynamic` 节区规范化（`0015271e`）已闭环，但仅限于"链接产物静态校验"层面；
- rtld e2e **实跑验证**（PIE + 真实 ld.so：`mt/ld -pie -dynamic-linker` → 加载 → exit 42）需合并 `tmp/rtld-p0`（含 `-dynamic-linker`，commit `f88ae83` 的 check-rtld 端到端门禁）后由 exec-integration 跨域门禁覆盖；
- 关联 commit：`0015271e`（.dynamic 节区）、`f88ae83`（check-rtld 端到端）、`tmp/rtld-p0` 4 提交（153be27/3f2c354/82a4b40/f88ae83）。

## 验收纪律：PIC/GOT 改动必须跑 verify-all.sh（2026-08-04）

> 教训来源：i386-pic-got 分支 `check-mir g_pic undefined` 漏检，由 exec-mcc-lite 在合入主分支时发现，commit `2d4b65a` 修复。

- **PIC/GOT 相关 mcc 改动不能只跑 `check-pic-verify`**，必须**同时跑 `verify-all.sh`（含 check-mir）**；
- 否则会漏检 **MIR 单测链接缺陷**：本次即 MIR 单测引用 `g_pic` 未定义（只跑 check-pic-verify 未触发，verify-all 的 check-mir 才暴露）。

### MIR 层强定义纪律

- **MIR 层需被单测链接清单强引用的全局（如 `g_pic`）必须定义在 `src/mir/machine.c`**；
- 不得只在 target `memit.c` 定义——`check-mir-*` 手工链接清单含 `machine.c`、**不含** `main.c` 也不含 `memit.c`，放在后者会链接失败漏检；
- PIC 改动前确认目标全局归属 MIR 层时，放到 machine.c 并同步跑 `verify-all.sh` 的 check-mir 门禁验证。

## P1 GD TLS 进度（2026-08-04 集成验证）

- **合并零回归**：`P1a`（commit `4948044`）+ `P1b`（commit `9b5b6e6` / `f65590c` / `d0b1e70`）已合并，门禁零回归。
- **已闭环（静态/链接期）**：
  - `D1`：`@PLT` 大小写（commit `f65590c`）；
  - 静态 `GD→LE`（general dynamic → local exec）放松。
- **进行中（GD 运行期）**：
  - `D2`：exec-toolchain **JUMP_SLOT 收集**（commit `d0b1e70` 已提交）；
  - `D3`：exec-mcc **GD store clobber**（修复中）。
- **集成复测**：由 exec-integration-lite 负责（GD 运行期跨 mcc + mt/ld 端到端门禁）。

> **本轮并行进展延伸**（TLS 静态 GD 运行期闭环、m++ C++ P1 5 项、P0.1 动态 libc、mt/ld .dynamic 新阻塞、静态数组 segfault、关键教训）见 [project_mcc_toolchain_roundup.md](project_mcc_toolchain_roundup.md)。


## i386 返回非负整常量错值（2026-08-05 闭环）

> 教训来源：mt 验证线 check-qemu-i386 hello.c 返回 0 非 42。根因在 **mcc i386 后端**，非 mt。

- **根因**：`i386_mabi.c` `mabi_selret` 的 i64 返回分支无条件从 `[ebp + s0->slot]` 读返回值，未处理 `MV_CONST` 立即数。
  - `return 42` → MIR `ret $c0`（`(i64)42` 常量，无 slot）→ `s0->slot` 未定义 → `movl <垃圾>(%ebp), %eax` 读未初始化栈 → 返回 0。
  - `return -1` **侥幸**被前端整成 `neg(1)` i32（走 else 分支 MOV）而绕开 i64 分支，掩盖了 bug。
- **修复**：i64 返回、`s0->kind==MV_CONST` 时拆低/高 32 位 i32 立即数 `MMOP_MOV` 到 **EDX（高半）再 EAX（低半）**；实值保留原 slot LOAD。EAX 后写关键——memit 的 i32 const→mov 经 `%eax` 中转清 eax，先 EDX 后 EAX 让返回低半最后落到 EAX。
- **关键纪律**：
  1. **mabi/isell 里 slot 假设不适用于常量**——凡"从 slot 加载"路径都要问：源可能是 MV_CONST 吗？常量无 slot。
  2. **memit 的 i32 MOV 依赖 %eax 作 scratch 中转**，若目标寄存器也是保管关键值的返回寄存器需注意写入顺序。
  3. **负常量 vs 非负常量的镜像陷阱**：前端把 `-1` 折叠成 `neg(1)`（i32）绕过 i64 路径，导致"负的看起来对"掩盖"非负错"。改动返回/常量后端时，正负都要测。
- commit：`b6fb898`（mcc/i386），合入 main `58af57a`；mt 侧门禁 taker `check-qemu-i386` PASS (exit=42)。

## arm mcc 产物 mt/as 组装失败：双层缺陷（2026-08-05 闭环）

> 教训来源：mt 验证线 check-qemu-arm 在 `mt/as assembly` 阶段失败。根因**双层**：mt/as arm 前端缺口 + mcc arm 后端同型 bug（与 i386 b6fb898 一致）。

- **层 1（mt/as，meuos-toolchain）**：mcc arm 后端用 `fp`（=r11，AAPCS 帧指针）别名 + 出 `.syntax unified`/`.arch`/`.fpu` 使能伪指令：
  - `reg_num()`（arm/encode.c）知 r0-r15/sp/lr/pc，**缺 `fp`** → 所有含 `fp` 的栈帧指令报 unsupported；
  - `parse_directive`（as_parse.c）不认 `.syntax` 等 → 第 1 行即报错。
- **层 2（mcc，arm_mabi.c）**：`mabi_selret` i64 返回支分**无条件从 `[r11+s0->slot]` 读**，未处理 MV_CONST——与 i386 `i386_mabi.c` 完全同源（`return 42` 读未初始化帧）。`-1` 又因 frontend neg(1) i32 绕开。
- **修复**：
  1. mt/as：`reg_num` 加 `fp→11`；`parse_directive` no-op `.syntax/.arch/.fpu/.cpu/.object_arch/.eabi_attribute`（纯指令风格，不产数据）。
  2. mcc arm：i64 返回、MV_CONST 时拆低/高 32 位 `mout(MMOP_MOV, MT_I32, r1/r0, const)`；实值保留 slot load。arm 中转 r10/r12（非返回寄存器），无需 i386 的顺序讲究。
- **关键纪律**：
  1. **`fp` 是 AAPCS 帧指针别名（r11）**，任何 arm 汇编器/反汇编器都必须支持；缺它则一切带栈帧的指令全炸。
  2. **mabi/isell 的 slot 假设不适用 MV_CONST** 是**跨架构类 bug**（i386/arm 同型）——排查:谓"从 slot 加载返回值/参数"路径都要问源是否可能 MV_CONST。
  3. **负立即数**：mt/as arm 的 `add/mov #-N` 尚不支持（报 unsupported），mcc 统一语法会生成 `add #-1`（GNU as 接受）。这是 arm mt/as 前端**仍开放**的完善点（varargs 回归仍在负立即数处卡）。
- commit：mt/as `546e5af` + mcc arm `a25cf3c`，合入 main `c72597e`；`check-qemu-arm` PASS (exit=42)。

## mt/objcopy -O 输出格式（2026-08-05 闭环）

- **实现**：`objcopy -O binary/ihex/srec`，将 loadable 节区（SHF_ALLOC 非 NOBITS）按地址折叠输出。commit 5b621b3 合 a28187b。
- **对齐 GNU 的关键**：
  1. **binary**：从最低可加载地址铺到最高结束、gap 补 0（文件大小 = 末节区结束 - 首节区基址）。
  2. **ihex**：data 记录 type 00 + 节区地址>0xFFFF 时发 type 04 扩展地址（段切换）；EXEC 发 type 05 起始地址；EOF type 01。
  3. **srec**：按地址幅度选 S1(16)/S2(24)/S3(32) 数据记录，S5 计数 + S9/S8/S7 终止（地址宽度对应）。
- **踩坑**：ihex **type 04 校验和最容易错**——初版只算 data 部分，漏了 len(02)+addr(0000)+type(04) 前缀字节，与 GNU 的 BA 差。校验和必须对整行（含前缀）取 `~sum+1 & 0xff`。
- **与 -j 组合**：`-O binary -j .data` 只输出 .data（keep 决策复用），fold 逻辑按 keep+alloc 节区收集。
- **纪律**：三种格式输出一致性靠"逐节区折叠 + gap 不补（ihex/srec）或补 0（binary）"——ihex/srec 不发 gap 的零记录（GNU 语义），否则体积爆炸（4KB gap 铺成几百行零记录）。


## mt 门禁 SKIP/FAIL 语义 + qemu 掩码（2026-08-05，大喵"门禁不盖问题"指令）

- **掩码 bug**：`check-qemu-aarch64/riscv64/loongarch64/arm` 曾把**任何非零 pipeline rc 映射为 "⚠️ SKIP (partial failure)" 且 continue（rc=0）** → 真实跨架构断裂（mcc -S/as/ld/错误 ELF 架构/错 exit）被当 SKIP 隐藏，`make check` 仍 PASS。fix 101d5fc 已合 7f53ef9。
- **门禁语义铁律**：
  1. **仅** 环境缺失（qemu binary 不存在、sysroot 目录不存在）→ SKIP 且 `exit 0`（真环境限制）。
  2. **其余任何 failure** → 传播非零 rc，make 报 FAIL 停（不 try-catch、不吞）。
  3. benchmark 脚本（test/*.sh）返回精确 0/1，target recipe 必须让它冒泡（Recipe 最后一个命令 rc 传播），不能 `rc=?; if ...` 把失败重写为 SKIP。
- **宏统一**：多架构同构 target 用 make `define RUN_QEMU_ARCH` 宏 + `$(call)` 参数化，避免 6 处手抄漂移（`.PHONY`、check 依赖列表都要含）。
- **as 门禁健壮化（check-as-arm-neg / test/as_arm_neg.sh 教训）**：
  - `readelf -S` 取节区偏移字段是第 6 列（NOT $5，$5 是 sh_addr）；awk 模式用 `$3==".text"` 精确匹配避免误配其它区。
  - 指令字大小写敏感比较 → `tr a-f A-F` 统一大写。
  - 脚本 `cd $tmproot` 后，传入的相对路径参数（as/readelf）会失联 → 开头把参数 abs-olutize；对 PATH 命令用 `command -v` 解析。
- **教训根因**：门禁是否"真实"取决于它能否把内部 stage 失败语义无损传到 make exit code。任何把"被测对象失败"改写为"环境缺失"的映射都是掩码，必须禁。
