# B.6 MIR 完备验收 — 差距清单与修复记录

> 分支：`worktree-mxx-work`。日期：2026-08-02。
> 验收标准：计划文件 `cosmic-forging-einstein-Cjwtv5Wd.md` §B.6 六项。

## 审查结论

| # | 验收项 | 状态 | 差距与修复 |
|:---:|:-------|:-----|:-----------|
| 1 | 类型完备（mir-type-matrix 100%） | ✅ 达标 | `_Decimal*`/`_BitInt(>64)` 为 C23 待扩展项，**非阻塞**（m++ 目标不含）；矩阵已标注。验证 `make check-mir-types`。 |
| 2 | SSA 显式（MPhi 唯一跨块合并 + ssacheck 全绿） | ✅ 达标（本轮补） | **差距：MIR 层原无 ssacheck**。补 `mssa_check`（src/mir/ssa.c）+ `MIR_PASS_SSA`，`run_mir_passes` 末尾强制门禁。修复 bridge_test fib 的未定义值 c2。验证 `make check-mir`。 |
| 3 | 扩展机制（MIns.extra/MTypeDesc.ext 空载实验） | ✅ 达标（本轮补） | **差距：无空载实验验证**。补 `test_extra`（mir_test）：MOP_EXTRA + extra=0x12345678 存储/dump 渲染/mssa_check 通过；MTypeDesc.ext 承载。文档记录于 mir-spec.md §3.8。 |
| 4 | C 前端全量迁移（make check 全绿 + 自举） | ✅ 达标 | verify-all 6/6（check/c99/c11/cpp/mir + check-sysroot-static 自举）。 |
| 5 | 性能达标（.text≤15KB/栈帧≤400B/mov≤2000） | ⏳ 长期 | 当前 decompress .text 49.7KB/栈帧 3976B/mov 6905。根因=rega spill（B.3 分析，P2 长期专项）。**不阻塞其他验收**。 |
| 6 | 文档交付（mir-spec.md + mir-type-matrix.md） | ✅ 达标（本轮补） | 两文档已存在；本轮更新状态行、SSA 门禁、扩展空载记录。 |

## 本轮修复项（提交内容）

1. **src/mir/ssa.c（新）**：`mssa_check` — 验证每 MV_TEMP 单 def（MIns.def XOR MPhi.defphi）、phi arg/blk 配对、指令源引用有效（值/常量表内）。
2. **src/mir/passes.c**：`MIR_PASS_SSA` case + `run_mir_passes` 末尾强制 `mssa_check`（违反报 `SSA consistency check FAILED`）。
3. **include/mir.h**：声明 `mssa_check`，枚举 `MIR_PASS_SSA`。
4. **test/mir/mir_test.c**：`test_ssa_phi`（phi 合并 fn 通过 + 故意双定义被捕获）、`test_extra`（扩展槽空载实验）。
5. **test/mir/bridge_test.c**：删除未定义的 c2 值（真实 MIR 通过 mssa_check 后桥接验证）。
6. **docs/mir-spec.md / docs/mir-type-matrix.md**：状态更新 + SSA 门禁 + 扩展空载记录。

## 验证

- `make check-mir-types` / `make check-mir`：全 PASS（含新增 mssa_check/extra 测试）。
- verify-all 6/6（自举 PASS）。

## 后续（非阻塞）

- 性能项（#5）：P2 rega/spill 长期专项，路径见 docs/mir-backend/b3-analysis.md。
- C23 `_Decimal*`/`_BitInt(>64)`：按需补充（m++ 高级标准时）。
