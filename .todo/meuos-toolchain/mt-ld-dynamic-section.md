# mt/ld 生成的 .dynamic 节区不符合 ELF 规范（sh_type=PROGBITS / sh_link=0）

> 状态：✅ 完成（2026-08-04 exec-toolchain-lite-2，commit 0015271e）
> 关联 commit：49349b2（readelf 端已做兼容回退，本任务从根因修正）

## 现象

`src/ld` 生成动态链接产物时，`.dynamic` 节区被写为：
- `sh_type = SHT_PROGBITS`（规范应为 `sh_type = SHT_DYNAMIC`，值 6）；
- `sh_link = 0`（规范应指向 `.dynstr` 节区索引）。

导致外部/标准工具无法按 ELF 规范定位 `.dynamic` 语义，迫使 readelf 等做非标兼容（见 commit 49349b2 的 `sh_type=PROGBITS` 回退逻辑）。

## 影响

- `mt/readelf -d`、GNU readelf、strip/objcopy 等依赖 `sh_type==SHT_DYNAMIC` + `sh_link` 的工具解析异常；
- 当前 readelf 已做回退兼容，但属"治标"：继续依赖兼容分支而非规范节区，长期易引入更多工具误判。

## 根因假设（待 exec-toolchain 实施时核实）

- `src/ld/elfwriter.c` 生成 `.dynamic` section group 时**未设置 `sh_type = SHT_DYNAMIC`（6）**，也未设置 `sh_link = <dynstr index>`；
- 可能复用通用 section 写入路径，默认落入 PROGBITS（1）且 sh_link 未显式赋值。

## 验收

- `mt/ld` 链接动态/共享产物后，`mt/readelf -S` 显示 `.dynamic` 为 `sh_type=DYN`；
- `sh_link` 指向 `.dynstr` 节区索引（readelf -S 的 Link 列正确）；
- 既有 `make -C projects/meuos-toolchain check`（含 check-ld/check-rtld）全 PASS，无新 warning（-Werror）；
- 回归：`mt/readelf -d` PIE 仍正常（即使去掉 PROGBITS 兼容分支也应通过）。

## 范围约束

- 仅 `src/ld/*`（重点 `src/ld/elfwriter.c`）与 Makefile；
- 不动 `src/rtld/*` / `src/readelf/*` / `src/as/*` / `src/libelf/*`；
- 不引入新 warning（-Werror）；
- 文件级 git commit，格式 `mt: ld: ...`。

## 修复记录（2026-08-04 exec-toolchain-lite-2）

- **commit**：`0015271e`（分支 `tmp/exec-toolchain/ld-dynamic-section`），msg：`mt: ld: set SHT_DYNAMIC and sh_link for .dynamic section`
- **文件**：`src/ld/link.c`（**根因更正**：`src/ld/` 下无 `elfwriter.c`，仅 `link.c`；`.dynamic` 生成在 `ensure_dynamic_section`（L3830）与 section-header 写循环（L2839））。
- **关键 diff（3 段）**：
  - **L3830**：`MT_SHT_PROGBITS` → `MT_SHT_DYNAMIC`（修复 sh_type）；
  - **L2839**：条件 `if (ctx->shared)` → `if (ctx->shared || ctx->pie)`，并加 `.dynamic` 段号收集 + `sections[dynamic_sec].link = dynstr_sec`（修复 sh_link；顺带补 PIE 下 `.dynsym` 的 sh_link→`.dynstr`）。
- **验证矩阵（PIE 与 shared 两种 ET_DYN）**：
  - `.dynamic` 段均 `sh_type=DYNAMIC`；
  - `sh_link` → `.dynstr`（readelf -S Link 列正确）；
  - `make -C projects/meuos-toolchain check` 全 PASS，warning=0。
- **rtld e2e 限制**：当前基线 df962a0 不含 rtld-p0 的 `-dynamic-linker` 选项，故 rtld e2e 运行验证需合并 rtld-p0 后由 exec-integration 端到端门禁覆盖（建议登记为后续任务）。
