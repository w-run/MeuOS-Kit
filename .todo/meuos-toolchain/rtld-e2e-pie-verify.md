# rtld e2e 实跑验证需合并 rtld-p0 后由 exec-integration 门禁覆盖

> 状态：🔄 开放（2026-08-04 由 doc-pm 登记，源自 mt-ld-dynamic-section 修复记录的 rtld e2e 限制）
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
