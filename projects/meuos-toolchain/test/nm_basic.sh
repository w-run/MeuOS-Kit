#!/bin/sh
# nm_basic.sh - 验证 nm 工具基本功能
# Usage: nm_basic.sh <nm> <test-obj>
set -eu
NM="${1:?usage: nm_basic.sh <nm> <test-obj>}"
OBJ="${2:?usage: nm_basic.sh <nm> <test-obj>}"

export LC_ALL=C

# 1. --version
"$NM" --version | grep -q '^meuos-toolchain nm 0\.2\.0'

# 2. --help
"$NM" --help >/dev/null

# 3. 读取测试 .o, 至少输出符号
out=$("$NM" "$OBJ")
test -n "$out"

# 4. 存在已定义的 FUNC 符号 (T = defined FUNC global)
# hello.c 提供 toolchain_fixture_value 函数
echo "$out" | grep -Eq '[[:space:]]T[[:space:]]+toolchain_fixture_value$'

# 5. -n numeric sort 不崩
"$NM" -n "$OBJ" >/dev/null

# 6. -g extern only 不崩
"$NM" -g "$OBJ" >/dev/null

# 7. -S print size 不崩
"$NM" -S "$OBJ" >/dev/null

# 8. -f posix 格式
"$NM" -f posix "$OBJ" >/dev/null

# 9. 默认不显示 NULL 符号行 (无空 name 行)
echo "$out" | grep -Eq '^[[:space:]]+U[[:space:]]*$' && {
    echo "FAIL: NULL symbol leaked"; exit 1; }

echo "mt nm basic: PASS"
