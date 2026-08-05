#!/usr/bin/env bash
#
# verify-all.sh — mcc/m++ 一键回归验收门禁
#
# 聚合执行以下检查，任一失败即以非零状态退出：
#   1. check             冒烟：构建 mcc 并编译运行 hello
#   2. check-mir         MIR 核心单元测试（types/passes/machine/abi/regalloc/bridge）
#   3. check-cpp         m++ C++ 前端（lex/virtual/func/neg，含虚表与模板）
#   3b. check-cpp × MIR-native/bridge 双后端显式复验（MCC_MIR_BACKEND=1/=0
#       各跑一次 check-cpp-func/neg；Phase 2 后默认即 MIR-native，显式
#       变体堵住"只覆盖单一后端"的覆盖缺口）
#   4. check-c99         C 回归（c99 套，--specs=host 或 MEUOS_SYSROOT 模式按当前环境可用性）
#   5. check-c11         C 回归（c11 套）
#   6. check-c23         C 回归（c23 套，同样按环境选择 MEUOS_SYSROOT / --specs=host）
#   7. check-abi         聚合体 ABI 回归（相邻位域共享存储单元不膨胀布局）
#   8. check-driver      driver 回归（sysroot 解析 + feature-regress）
#   9. check-mt-integration MT_AS/MT_LD/MT_AR 重定向到 meuos-toolchain（未构建时自行 SKIP）
#  10. check-i386 / check-loongarch64 / check-targets  交叉目标汇编回归
#  11. check-arm / check-i386-runtime / check-aarch64-runtime  交叉运行时回归
#                       （各自缺 sysroot / qemu 时脚本内自行 SKIP，退出 0）
#  12. check-sysroot-static 自举：mcc 编译 mcc + 运行 hello
#  13. check-c-mir       C 功能回归 × MIR/LIR 双路径矩阵（mir_matrix.sh：MIR=1 与 MIR=0 编译运行且 stdout 一致）
#
# 未纳入本门禁的目标及原因：
#   check-pic-verify     PIC GOT 验证（x86_64/aarch64/riscv64/i386 四架构
#                        已修复通过；独立目标，未纳入本汇总门禁）
#   check-chibicc        社区套件（chibicc）当前 41/41 编译失败，见
#                        test/community/chibicc/REPORT.md 的分类
#   check-i386-qemu      需 qemu-system-i386 整机 VM，耗时过长，不适合门禁
#   check-c11-atomic     已被 check-c11 全量覆盖
#
# 用法：
#   sh test/verify-all.sh [--verbose]
#
# 不修改任何源码；仅新增本脚本与 Makefile 的 check-all 目标。

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

SYSROOT="${SYSROOT:-$ROOT/../sysroot}"
BIN=./mcc

# 默认 specs 为 meuos：当 libc-meuos.a 可用时走 MEUOS_SYSROOT 模式，
# 否则 C 回归退化为 --specs=host（宿主 libc 编译运行，任何环境可跑）。
if [ -f "$SYSROOT/usr/lib/libc-meuos.a" ]; then
    SYSROOT_OK=1
else
    SYSROOT_OK=0
fi

PASS=0; FAIL=0; SKIP=0
declare -a RESULTS

# run <label> <cmd...> — 执行并记录 PASS/FAIL
run() {
    local label="$1"; shift
    echo "==== $label ===="
    if "$@"; then
        echo "PASS  $label"
        RESULTS+=("PASS  $label")
        PASS=$((PASS + 1))
    else
        echo "FAIL  $label"
        RESULTS+=("FAIL  $label")
        FAIL=$((FAIL + 1))
    fi
    echo
}

# --specs=host 回退：复刻 Makefile check-c99 逻辑（extern 需附带 extern_defs.c）
run_c99_host() {
    local out
    for t in test/c99/*.c; do
        out="/tmp/mcc-verify-$(basename "$t" .c)"
        case "$t" in
            *extern_defs*) true ;;
            *extern*) "$BIN" --specs=host -Itest/c99 -o "$out" "$t" test/c99/extern_defs.c || return 1
                      "$out" || return 1 ;;
            *)        "$BIN" --specs=host -Itest/c99 -o "$out" "$t" || return 1
                      "$out" || return 1 ;;
        esac
    done
}

# --specs=host 回退：复刻 Makefile check-c11 逻辑（atomic/thread_local 需 shim + -lpthread）
run_c11_host() {
    local out
    for t in test/c11/*.c; do
        out="/tmp/mcc-verify-$(basename "$t" .c)"
        case "$t" in
            *atomic*|*thread_local*) "$BIN" --specs=host -Itest/c11 -o "$out" "$t" -lpthread || return 1 ;;
            *varargs*)              "$BIN" --specs=host -Itest/c11 -o "$out" "$t" || return 1 ;;
            *)                      "$BIN" --specs=host -o "$out" "$t" || return 1 ;;
        esac
        "$out" || return 1
    done
}

# --specs=host 回退：复刻 Makefile check-c23 逻辑（默认 specs 为 meuos，
# 需链接 -lc-meuos；无 sysroot 时改走宿主 libc）
run_c23_host() {
    local out
    for t in test/c23/*.c; do
        out="/tmp/mcc-verify-c23-$(basename "$t" .c)"
        "$BIN" --specs=host -Itest/c23 -o "$out" "$t" || return 1
        "$out" || return 1
    done
}

# --specs=host 回退：复刻 Makefile check-abi 逻辑
run_abi_host() {
    "$BIN" --specs=host -o /tmp/mcc-verify-bitfield-aggregate \
        test/abi/bitfield_aggregate.c || return 1
    /tmp/mcc-verify-bitfield-aggregate
}

echo "== mcc/m++ 一键验收 (root=$ROOT) =="
[ "$SYSROOT_OK" = 1 ] && echo "sysroot: $SYSROOT (MEUOS_SYSROOT 模式)" \
                      || echo "sysroot: 未找到 libc-meuos.a (C 回归走 --specs=host 回退)"
echo

# 1. 冒烟
run "make check" make check

# 2. MIR 单元测试
run "make check-mir" make check-mir

# 3. m++ C++ 前端（virtual 需 sysroot；缺失时仅跑 lex/func/neg）
if [ "$SYSROOT_OK" = 1 ]; then
    run "make check-cpp" make check-cpp
else
    run "make check-cpp (lex/func/neg, 跳过 virtual)" \
        make check-cpp-lex check-cpp-func check-cpp-neg
fi

# 3b. C++ 套件 × MIR-native/bridge 双后端显式复验（Phase 2 后默认即
#     MIR-native，此处显式锁定 MCC_MIR_BACKEND=1/0，防环境变量或未来
#     默认值变化漏网——补上"verify-all 不设 MCC_MIR_BACKEND 导致 cpp
#     套件只覆盖单一后端"的缺口）。
run "make check-cpp-func/neg (MCC_MIR_BACKEND=1)" \
    env MCC_MIR_BACKEND=1 make check-cpp-func check-cpp-neg
run "make check-cpp-func/neg (MCC_MIR_BACKEND=0)" \
    env MCC_MIR_BACKEND=0 make check-cpp-func check-cpp-neg

# 4. C 回归
if [ "$SYSROOT_OK" = 1 ]; then
    run "make check-c99 (MEUOS_SYSROOT)" env MEUOS_SYSROOT="$SYSROOT" make check-c99
    run "make check-c11 (MEUOS_SYSROOT)" env MEUOS_SYSROOT="$SYSROOT" make check-c11
else
    run "check-c99 (--specs=host 回退)" run_c99_host
    run "check-c11 (--specs=host 回退)" run_c11_host
fi

# 5. 补充门禁：C23/ABI/driver/MT 集成 + 交叉目标汇编回归
#    （check-c23 与 check-abi 默认 specs 为 meuos、需链接 -lc-meuos，
#     故按 sysroot 可用性在 MEUOS_SYSROOT 模式与 --specs=host 回退间选择。
#     check-mt-integration 在 meuos-toolchain 未构建时自行 SKIP。）
if [ "$SYSROOT_OK" = 1 ]; then
    run "make check-c23 (MEUOS_SYSROOT)" env MEUOS_SYSROOT="$SYSROOT" make check-c23
    run "make check-abi (MEUOS_SYSROOT)" env MEUOS_SYSROOT="$SYSROOT" make check-abi
else
    run "check-c23 (--specs=host 回退)" run_c23_host
    run "check-abi (--specs=host 回退)" run_abi_host
fi
run "make check-driver" make check-driver
run "make check-mt-integration" make check-mt-integration
run "make check-i386" make check-i386
run "make check-loongarch64" make check-loongarch64
run "make check-targets" make check-targets

# 6. 交叉运行时回归：各脚本在缺 sysroot / qemu 时自行打印 skipping 并退出 0，
#    因此可无条件纳入门禁——环境齐备时它们才真正执行并守护 ABI 行为。
run "make check-arm" make check-arm
run "make check-i386-runtime" make check-i386-runtime
run "make check-aarch64-runtime" make check-aarch64-runtime

# 7. 自举（内部自行构建 sysroot，任何环境下均执行）
run "make check-sysroot-static" make check-sysroot-static

# 8. C 功能回归 × MIR/LIR 双路径矩阵（mir_matrix.sh：MIR=1 与 MIR=0 均编译运行且 stdout 一致）
run "make check-c-mir" make check-c-mir

# 9. 本轮已闭合的 new/delete、braced-init、析构、虚/纯虚机制聚焦回归 batch
#    （test/cpp/focus_regress.sh：按语义域拆细，逐域 PASS/FAIL 可定位）
run "make check-cpp-focus" make check-cpp-focus

# 10. m++ vs g++ 语义对照 batch（test/cpp/gcc_compare.sh；g++ 缺失时自 SKIP，
#     双编译器都接受的测试要求 exit code 一致）
run "make check-cpp-gcc" make check-cpp-gcc

# 11. x86_64 静态 runtime 回归（test/x86_64/runtime.sh：静态 exe 数组地址
#     路径 + 算术；缺 sysroot 时自 SKIP，本机 x86_64 具备时真正执行）
run "make check-x86_64-runtime" make check-x86_64-runtime

# 12. riscv64 / loongarch64 静态 runtime 回归（test/<arch>/runtime.sh：静态
#     交叉 exe + user-mode qemu；缺交叉 sysroot 或 user-mode qemu 时自 SKIP）。
#     loongarch64 编译路径已验证通过；riscv64 全局数组 emit 有 pcrel 链接
#     bug（见 runtime_arr.c 注释），本 batch 用局部数组源，全局数组 ABI 由
#     x86_64/loongarch64 runtime 覆盖。
run "make check-riscv64-runtime" make check-riscv64-runtime
run "make check-loongarch64-runtime" make check-loongarch64-runtime

# 汇总
echo "======================================"
echo "汇总: PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
for r in "${RESULTS[@]}"; do
    echo "  $r"
done
echo

if [ "$FAIL" -gt 0 ]; then
    echo "验证未通过，存在失败项"
    exit 1
fi
echo "全部通过"
exit 0
