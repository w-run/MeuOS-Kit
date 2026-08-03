# mcc atomic 窄类型符号扩展缺陷

> 来源：2026-08-04 接手审计（大喵指示验证实际进度）
> 严重度：🔴 高（阻塞 libc `make check`）
> 组件：mcc（`_Atomic` 代码生成）

## 现象
`projects/meuos-libc` 的 `make check` 失败（rc=2），`test/atomic.c` 输出 `FAIL` exit=1。
对照：宿主 gcc 编译同文件 `PASS`。

## 根因
mcc 对 `atomic_short`（`_Atomic short`）的 `atomic_fetch_add` 返回值**零扩展**而非**符号扩展**。

- `test/atomic.c` line 21: `atomic_short small = -2;`
- line 46: `atomic_fetch_add(&small, 3) != -2` 断言
- mcc 实测：`atomic_fetch_add(&small, 3)` 返回 `65534`（0xFFFE 零扩展到 int），而非 `-2`（符号扩展）
- `65534 != -2` → true → 误判进入 FAIL 分支

分段验证（mcc 编译，均正确）：`atomic_store`/`atomic_load`/`atomic_fetch_or`/`atomic_exchange`/`atomic_compare_exchange_strong`/`atomic_flag_test_and_set`/`atomic_fetch_sub`(long)/`thrd_create`/`counter` 全部正确；**仅窄类型（short/char）`fetch_add` 返回值符号扩展错误**。`atomic_long`（64 位）符号扩展正常。

## 复现
```sh
cd projects/meuos-libc
export MEUOS_SYSROOT=/workspace/MeuOS-Kit/sysroot
make check  # rc=2, atomic FAIL
# 或直接：
../mcc/mcc --specs=meuos --sysroot=$MEUOS_SYSROOT -o /tmp/atomic test/atomic.c
/tmp/atomic  # 输出 FAIL, exit=1
# 定位：
cat > /tmp/loc.c <<'EOF'
#include <stdatomic.h>
#include <stdio.h>
int main(void){ atomic_short s=-2; printf("%d\n",(int)atomic_fetch_add(&s,3)); return 0; }
EOF
../mcc/mcc --specs=meuos --sysroot=$MEUOS_SYSROOT -o /tmp/loc /tmp/loc.c
/tmp/loc  # 期望 -2，实际 65534
```

## 修复方向
- 检查 mcc 前端/后端对 `_Atomic` 窄整数（char/short）`__atomic_fetch_add` 返回值的符号扩展处理
- 对比 `atomic_long`（正确）与 `atomic_short`（错误）的代码生成差异
- 参考实现：musl `stdatomic.h` 内联 + mcc `__atomic_*` 内建 lowering

## 验收标准
- [ ] `atomic_fetch_add` 对 `atomic_short`/`atomic_char`/`atomic_uchar` 返回值正确符号扩展
- [ ] `projects/meuos-libc/test/atomic.c` mcc 编译运行输出 `PASS` exit=0
- [ ] `make -C projects/meuos-libc check` rc=0
- [ ] `verify-all.sh` 仍 19/19（无回归）

## 备注
- verify-all 19/19 未覆盖 libc atomic 全流程，故此缺陷被掩盖
- 修复后建议把 libc atomic 全流程纳入门禁或周期审计
