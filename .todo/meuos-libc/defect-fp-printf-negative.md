# libc vfprintf 浮点格式化缺陷（负数+精度）

> ✅ **已闭环（2026-08-05 验证）**：由 `0fc6ff5`（sign_bit 死循环）、`da205fa`（%f/%e 舍入点）、`c004de8`（mcc x86_64 负零池区分 +0.0/-0.0）等收敛修复。
> 验证：`projects/mcc/mcc --specs=meuos` 编译运行 `test/fp_fmt.c` → **PASS fp_fmt，0 FAIL**；最小复现 `printf("%.2f", -3.14)` 正确输出 `-3.14`。
> 保留本文件作为回归记录；如需从待办清出可删除。

> 来源：2026-08-04 接手审计修复 atomic 缺陷后暴露（mcc-toolchain HEAD `7ff1fc5`）
> 严重度：🟡 中（浮点 printf 受影响，但正数 %f/%e 正常）
> 组件：meuos-libc（`vfprintf` / `__float_to_str`）

## 现象
`printf` 的 `%f`/`%e`/`%g` 在**负数 + 精度**组合下输出错误（输出 `0.00` 或符号丢失）。

`projects/meuos-libc` 的 `make check` 失败（rc=2），`test/fp_fmt.c` 报 **46 个 FAILURES**。

## 根因方向
- `printf("%.2f", -3.14)` 输出 `0.00`（应 `-3.14`）
- `printf("%f", 3.14)` 输出 `3.140000` ✅（正数正常）
- `printf("%e", 1.0e10)` 输出 `1.000000e+10` ✅（正数正常）
- mcc 汇编确认参数传递正确（`movsd .Lmain.lc0(%rip), %xmm0; call printf`）——**非 mcc 缺陷，是 libc 格式化缺陷**
- 根因在 libc `vfprintf` / `__float_to_str` 对**负数 + 精度**的处理：负号丢失或浮点值被当作 0

## 复现
```sh
cd projects/meuos-libc
export MEUOS_SYSROOT=/workspace/MeuOS-Kit/sysroot
make check  # rc=2, fp_fmt: 46 FAILURES
# 最小复现：
cat > /tmp/fp_min.c <<'EOF'
#include <stdio.h>
int main(void){ printf("%.2f\n", -3.14); printf("%f\n", 3.14); printf("%e\n", 1.0e10); return 0; }
EOF
../mcc/mcc --specs=meuos --sysroot=$MEUOS_SYSROOT -o /tmp/fp_mcc /tmp/fp_min.c
/tmp/fp_mcc
# mcc 输出: 0.00 / 3.140000 / 1.000000e+10（第一行错，应 -3.14）
# gcc 对照: -3.14 / 3.140000 / 1.000000e+10
```

## 修复方向
1. 定位 libc `vfprintf` 的 `%f`/`%e`/`%g` 格式化路径（`projects/meuos-libc/src/stdio/` 下 `vfprintf.c` 或 `printf.c`）
2. 检查 `__float_to_str`（或同类浮点转字符串函数）对负数的符号处理
3. 检查精度（`.2f` 的 `.2`）与负数组合的代码路径：是否精度处理时覆盖了符号位，或负数分支提前返回 0
4. 对照 musl `vfprintf.c` 的 `fmt_fp` 实现（musl 是零 GNU 依赖参考）
5. 注意 `test/fp_fmt.c` 注释提到"fp_fmt rounds half-up; glibc rounds half-to-even"——这是已知的舍入差异（0.5 %.0f），与本缺陷（负数输出 0.00）不同，勿混淆

## 验收标准
- [ ] `printf("%.2f", -3.14)` 输出 `-3.14`
- [ ] `printf("%+.2f", -3.14)` 输出 `-3.14`，`printf("%+.2f", 3.14)` 输出 `+3.14`
- [ ] `printf("%f", -3.14)`、`printf("%e", -3.14e10)`、`printf("%g", -0.5)` 负数全正确
- [ ] `projects/meuos-libc/test/fp_fmt.c` mcc 编译运行 0 FAILURES（除已知 half-up/half-even 舍入差异外）
- [ ] `make -C projects/meuos-libc check` rc=0
- [ ] `verify-all.sh` 仍 19/19（无回归）

## 备注
- 此缺陷被 libc `test/atomic.c` 失败掩盖（atomic 失败时 make check 在 atomic 处停止，fp_fmt 未运行）
- 2026-08-04 atomic 缺陷修复后（`fix/mcc-atomic-signext` `407d326`）才暴露
- 修复后建议把 libc `make check` 全流程纳入周期审计（当前 verify-all 19/19 不含 libc fp_fmt）
