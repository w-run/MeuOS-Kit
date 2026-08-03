#!/bin/sh
# test/runner.sh - meuos-utils 测试主入口
#
# 遍历 t_*.sh，每脚本独立执行，汇总 PASS/FAIL。
# 全脚本无 FAIL 行即整体 PASS，退出 0。

set -u
cd "$(dirname "$0")" || exit 1

. ./common.sh

# 工具路径：从 test/ 看 ../build
export LIBUTS_DIR="$(cd .. && pwd)/build"

# msh 路径：优先 ../build/msh（shell-utils worktree 中存在），否则 /bin/sh
if [ -x "../build/msh" ]; then
    export MSH="$(cd .. && pwd)/build/msh"
else
    export MSH="/bin/sh"
fi

echo ">>> meuos-utils test runner"
echo "    LIBUTS_DIR=$LIBUTS_DIR"
echo "    MSH=$MSH"
echo ""

total_pass=0
total_fail=0

for t in t_*.sh; do
    [ -f "$t" ] || continue
    echo "--- $t ---"
    out=$(./"$t" 2>&1)
    echo "$out"
    if echo "$out" | grep -q "^FAIL:"; then
        total_fail=$((total_fail + 1))
    else
        total_pass=$((total_pass + 1))
    fi
done

echo ""
echo "=== Total: $total_pass PASS / $total_fail FAIL ==="
[ "$total_fail" -eq 0 ]
