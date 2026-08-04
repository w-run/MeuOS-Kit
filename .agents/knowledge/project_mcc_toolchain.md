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

