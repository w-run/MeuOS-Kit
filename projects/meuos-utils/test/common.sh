#!/bin/sh
# test/common.sh - meuos-utils 测试框架 helper
#
# 用法（在每个 t_*.sh 顶部 source）：
#   . ./common.sh
#   t_setup "工具名"
#   assert_eq "got" "want" "case name"
#
# 全局变量由 runner.sh 提供：
#   LIBUTS_DIR  - build/ 目录（如 ../build）
#   MSH         - msh 可执行路径（如 ../build/msh 或 /bin/sh 兜底）

# 兜底：允许单脚本直接跑
: "${LIBUTS_DIR:=../build}"
: "${MSH:=/bin/sh}"

# 计数器（runner.sh 会重置，单跑时也可用）
T_PASS=${T_PASS:-0}
T_FAIL=${T_FAIL:-0}
T_SKIP=${T_SKIP:-0}

# 工具路径前缀：$U cat [args...] -> ../build/cat [args...]
# 用法：U echo hello world  =>  ../build/echo hello world
U() {
    local _tool="$1"
    shift
    "$LIBUTS_DIR/$_tool" "$@"
}

# 断言相等
assert_eq() {
    if [ "$1" = "$2" ]; then
        pass "$3"
    else
        fail "$3" "got=[$1]" "want=[$2]"
    fi
}

# 断言不相等
assert_ne() {
    if [ "$1" != "$2" ]; then
        pass "$3"
    else
        fail "$3" "got=[$1]" "want != [$2]"
    fi
}

# 断言退出码
assert_rc() {
    rc=$1; want=$2; name=$3
    if [ "$rc" = "$want" ]; then
        pass "$name"
    else
        fail "$name" "rc=$rc" "want rc=$want"
    fi
}

# 断言 stdout 包含子串
assert_match() {
    if echo "$1" | grep -q "$2"; then
        pass "$3"
    else
        fail "$3" "got=[$1]" "want match /$2/"
    fi
}

# 跳过：原因
skip() {
    T_SKIP=$((T_SKIP + 1))
    echo "SKIP: $1"
}

# 通过
pass() {
    T_PASS=$((T_PASS + 1))
    echo "PASS: $1"
}

# 失败：打印详情，不退出（runner 收尾汇总）
fail() {
    T_FAIL=$((T_FAIL + 1))
    echo "FAIL: $1 ($2; $3)"
}

# 汇总本文件
t_summary() {
    echo "  -> $T_PASS pass / $T_FAIL fail / $T_SKIP skip"
}
