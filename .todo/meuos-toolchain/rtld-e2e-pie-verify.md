# rtld e2e 实跑验证需合并 rtld-p0 后由 exec-integration 门禁覆盖

> 状态：✅ 完成（2026-08-04 exec-integration-lite）
> 关联 commit：0015271e（.dynamic 节区规范化）、f88ae83（check-rtld 端到端门禁）、tmp/rtld-p0 4 提交（153be27/3f2c354/82a4b40/f88ae83）

## 现象

- 当前基线 df962a0 **不含** rtld-p0 的 `-dynamic-linker` 选项，无法在 exec-toolchain / exec-integration-lite 域直接跑 rtld e2e 实跑；
- PIE + 真实 ld.so 的**端到端运行验证**需先合并 `tmp/rtld-p0`（HEAD=f88ae83）后才能覆盖。

## 影响

- `.dynamic` 节区规范化（commit 0015271e）已闭环（sh_type=SHT_DYNAMIC、sh_link→.dynstr），但仅停留在"链接产物静态校验"层面；
- 真实 PIE 经 `mt/ld -pie -dynamic-linker` 链接后到 ld.so 加载运行的**端到端行为**尚未在合并后分支上复核，属联动破坏风险点。

## 范围

- 合并 `tmp/rtld-p0` → `tmp/lead-doc-mir-baseline` 后，由 exec-integration-lite 跑一次端到端：
  `mcc` → `mt/as` → `mt/ld -pie -dynamic-linker /tmp/.../ld.so` → `qemu-run`（若 qemu 可用）→ exit 42 仍 PASS；
- 并验证与 `.dynamic` 节区规范化（commit 0015271e）联动无破坏。

## 验收

- `tmp/lead-doc-mir-baseline` HEAD 合并 `tmp/rtld-p0` 后，`make check` + `check-rtld` 全 PASS；
- `tmp/rtld-p0` 的 `check-rtld.sh` 在合并后分支上仍能跑通。

## 范围约束

- 由 exec-integration-lite / exec-integration 跨域门禁覆盖；
- 仅文档/门禁登记，不动 `src/*`（实施由 exec 域负责，doc-pm 只登记与追踪）。

## 验证记录（exec-integration-lite，聚合分支 tmp/lead-doc-mir-baseline HEAD=2d4b65a，2026-08-04）

- `make -C projects/meuos-toolchain check-rtld`：**mt rtld e2e: PASS**（build/bin 全量重建 warning=0；`make check` 全 PASS 含 check-rtld）。
- 手动跨域 e2e（mcc 参与）：mcc -fPIC -fpie → mt/as → `mt/ld -pie -dynamic-linker $work/ld.so` → qemu/内核 PT_INTERP 加载 ld.so → **exit=42 PASS**；
  readelf 校验 ET_DYN + PT_INTERP + `1 R_X86_64_RELATIVE`（mcc `global@gotpcrel` GOT 槽被 ld.so 正确重定位）。
- 与 `.dynamic` 节区规范化（0015271e，SHT_DYNAMIC + sh_link→.dynstr）联动无破坏。
- 结论：**待办可关闭**。`-dynamic-linker` 端到端运行链路在聚合后分支验证通过。
