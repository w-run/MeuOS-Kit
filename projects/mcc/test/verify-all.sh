#!/usr/bin/env bash
#
# verify-all.sh — mcc/m++ 一键回归验收门禁
#
# 聚合执行以下检查，任一失败即以非零状态退出：
#   1. check             冒烟：构建 mcc 并编译运行 hello
#   2. check-mir         MIR 核心单元测试（types/passes/machine/abi/regalloc/bridge）
#   3. check-cpp         m++ C++ 前端（lex/virtual/func/neg，含虚表与模板）
#   4. check-c99/check-c11 C 回归，--specs=host 或 MEUOS_SYSROOT 模式按当前环境可用性
#   5. check-sysroot-static 自举：mcc 编译 mcc + 运行 hello
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

# 4. C 回归
if [ "$SYSROOT_OK" = 1 ]; then
    run "make check-c99 (MEUOS_SYSROOT)" env MEUOS_SYSROOT="$SYSROOT" make check-c99
    run "make check-c11 (MEUOS_SYSROOT)" env MEUOS_SYSROOT="$SYSROOT" make check-c11
else
    run "check-c99 (--specs=host 回退)" run_c99_host
    run "check-c11 (--specs=host 回退)" run_c11_host
fi

# 5. 自举（内部自行构建 sysroot，任何环境下均执行）
run "make check-sysroot-static" make check-sysroot-static

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
