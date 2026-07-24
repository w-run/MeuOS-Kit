<!--
priority: P2
status: pending
start_ts: 2026-07-24
note: GD-TLS Gap 3: 在 targ.c/expr.c 中实现 -fPIC 和 --shared 时外部 _Thread_local 自动降级为 GD, 支持 -ftls-model={global-dynamic,initial-exec,local-exec} 和 -fno-plt
-->

# 实现 TLS 模型选择逻辑（-ftls-model= 支持与 -fPIC 降级）

## 背景

当前 mcc 所有 `_Thread_local` 变量都使用 LE (Local-Exec)，通过 `SThr` 直接访问。
当编译 `-fPIC` 或 `--shared` 时，外部 `_Thread_local` 变量必须使用 GD (General-Dynamic)
而不是 LE/IE，否则链接器无法生成正确的重定位。

## 需求

1. 在 `targ.c` 的 `loadtls()` 或等价函数中新增 TLS 模型选择逻辑
2. 默认行为：
   - 非 PIC 模式：外部 TLS → IE (`SExt|SThr`)，本地 TLS → LE (`SThr`)
   - `-fPIC` 或 `--shared` 模式：外部 TLS → GD (`SGenThr`)，本地 TLS → LE (`SThr`)
3. 支持 `-ftls-model=` 命令行参数：
   - `-ftls-model=global-dynamic` → 强制 GD
   - `-ftls-model=initial-exec` → 强制 IE（`-fno-plt` 别名）
   - `-ftls-model=local-exec` → 强制 LE
4. 与现有 emit GD 指令序列（`expand_gd_tls()` 在 `emit.c` 中）配合

## 关联文件

- `projects/mcc/src/sema/targ.c` — TLS 模型配置和 `loadtls()` 实现
- `projects/mcc/src/irgen/expr.c` — 表达式翻译中的 TLS 处理
- `projects/mcc/src/irgen/emit.c` — 已有 `expand_gd_tls()` 展开逻辑

## 参考实现

- musl/arch/x86_64/crt_arch.h — TLS 模型配置
- GCC `-ftls-model=` 文档

## 验收标准

```bash
# 1. Build succeeds
make -C projects/mcc -j4
# 2. Non-PIC extern TLS produces IE (not GD) — use echo, no heredoc
echo 'extern _Thread_local int ext_tls; int *get_ext(void) { return &ext_tls; }' > /tmp/tls_model_test.c; projects/mcc/mcc --target=x86_64 -S -o /tmp/tls_model_le.s /tmp/tls_model_test.c 2>/dev/null; ! grep -q '__tls_get_addr' /tmp/tls_model_le.s && echo "Non-PIC extern TLS uses IE: PASS"
# 3. -fPIC extern TLS produces GD
projects/mcc/mcc --target=x86_64 -fPIC -S -o /tmp/tls_model_gd.s /tmp/tls_model_test.c 2>/dev/null; grep -q '__tls_get_addr' /tmp/tls_model_gd.s && echo "-fPIC extern TLS uses GD: PASS"
# 4. Non-extern TLS stays LE in all modes
echo '_Thread_local int local_tls; int *get_local(void) { return &local_tls; }' > /tmp/tls_model_local.c; projects/mcc/mcc --target=x86_64 -fPIC -S -o /tmp/tls_model_local.s /tmp/tls_model_local.c 2>/dev/null; ! grep -q '__tls_get_addr' /tmp/tls_model_local.s && echo "Local TLS stays LE under -fPIC: PASS"
# 5. Full regression
make -C projects/mcc check
```
