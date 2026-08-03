#!/bin/sh
# test/t_smoke.sh - 烟雾测试，验证框架可跑
. ./common.sh

echo "=== smoke: --version / --help / 基本行为 ==="

# --version 输出包含 "meuos-utils"
out=$(U echo --version 2>&1 | head -1)
assert_match "$out" "meuos-utils" "echo --version 含 (meuos-utils)"

# --help 输出非空
U echo --help >/dev/null 2>&1
assert_rc $? 0 "echo --help 退出码 0"

# true 退出码 0
U true
assert_rc $? 0 "true 退出码 0"

# false 退出码非 0
U false
rc=$?
assert_rc 1 $rc "false 退出码非 0 (实际=$rc)" || true

# echo hello world
out=$(U echo hello world)
assert_eq "$out" "hello world" "echo hello world"

t_summary
