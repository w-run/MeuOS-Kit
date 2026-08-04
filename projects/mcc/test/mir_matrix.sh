#!/bin/sh
# mir_matrix.sh — C 功能回归 × MIR 单路径矩阵。
#
# mcc 自 Phase 2 起强制 g_use_mir=1（MCC_USE_MIR env 已移除），MIR 是唯一
# 的 asm 生产者；Phase 3e 又移除了 LIR 桥接层，machine backend 覆盖全部
# 6 架构。因此历史上「MIR=1/MIR=0 双路径对比」已无意义（MCC_USE_MIR=0
# 与 =1 走完全相同路径，对比恒等），本脚本降级为单路径 MIR 回归：
#   1) c99/c11/c23 三套正向用例全部经唯一 MIR 路径编译并运行；
#   2) 每个用例运行退出码均为 0（正向用例约定 0=全部通过）。
# 任一违反即非零退出。接入 Makefile `check-c-mir` 目标。
#
# 用法：sh test/mir_matrix.sh ./mcc

BIN=${1:-./mcc}
[ -x "$BIN" ] || { echo "mir_matrix: missing compiler $BIN"; exit 1; }
cd "$(dirname "$0")/.." || exit 1

fail=0
run_case() {   # run_case <dir> <file> <extra_flags...>
	dir=$1; file=$2; shift 2
	base=$(basename "$file" .c)
	out=/tmp/mirm-$base
	if ! "$BIN" --specs=host -o "$out" "$@" "$file" 2>/tmp/mirm-e; then
		echo "FAIL($dir/$base) MIR 路径编译失败: $(head -1 /tmp/mirm-e)"
		fail=1; return
	fi
	"$out" >/tmp/mirm-o 2>&1; rc=$?
	if [ "$rc" -ne 0 ]; then
		echo "FAIL($dir/$base) 运行失败: rc=$rc"
		fail=1; return
	fi
	echo "ok  $dir/$base (MIR 路径)"
}

for t in test/c99/*.c; do
	case "$(basename "$t")" in
		extern_defs.c) continue ;;            # 无 main，仅配套定义
		extern.c)      run_case c99 "$t" -Itest/c99 test/c99/extern_defs.c ;;
		*)             run_case c99 "$t" -Itest/c99 ;;
	esac
done

for t in test/c11/*.c; do
	case "$(basename "$t")" in
		*atomic*|*thread_local*) run_case c11 "$t" -Itest/c11 -lpthread ;;
		*varargs*)               run_case c11 "$t" -Itest/c11 ;;
		*)                       run_case c11 "$t" ;;
	esac
done

for t in test/c23/*.c; do
	run_case c23 "$t"
done

echo "mir_matrix: fail=$fail"
exit $fail
