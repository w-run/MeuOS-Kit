#!/bin/sh
# mir_matrix.sh — C 功能回归 × MIR/LIR 双路径矩阵。
#
# mcc 双覆盖目标：MIR 路径（MCC_USE_MIR=1，默认）与 legacy 直接 LIR
# 路径（MCC_USE_MIR=0）并存。既有 check-c99/check-c11/check-c23 只跑
# 默认路径，LIR 路径与两路径一致性长期无守护。
#
# 本脚本把 c99/c11/c23 三套正向用例分别在 MCC_USE_MIR=1 与 =0 下编译
# 并运行，要求：
#   1) 两路径都能编译成功；
#   2) 两路径运行退出码均为 0（正向用例约定 0=全部通过）；
#   3) 两路径 stdout 完全一致（行为等价性）。
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
	out1=/tmp/mirm-$base-m1
	out0=/tmp/mirm-$base-m0
	if ! MCC_USE_MIR=1 "$BIN" --specs=host -o "$out1" "$@" "$file" 2>/tmp/mirm-e1; then
		echo "FAIL($dir/$base) MIR=1 编译失败: $(head -1 /tmp/mirm-e1)"
		fail=1; return
	fi
	if ! MCC_USE_MIR=0 "$BIN" --specs=host -o "$out0" "$@" "$file" 2>/tmp/mirm-e0; then
		echo "FAIL($dir/$base) MIR=0 编译失败: $(head -1 /tmp/mirm-e0)"
		fail=1; return
	fi
	"$out1" >/tmp/mirm-o1 2>&1; rc1=$?
	"$out0" >/tmp/mirm-o0 2>&1; rc0=$?
	if [ "$rc1" -ne 0 ] || [ "$rc0" -ne 0 ]; then
		echo "FAIL($dir/$base) 运行失败: MIR=1 rc=$rc1, MIR=0 rc=$rc0"
		fail=1; return
	fi
	if ! cmp -s /tmp/mirm-o1 /tmp/mirm-o0; then
		echo "FAIL($dir/$base) MIR=1/MIR=0 输出不一致"
		fail=1; return
	fi
	echo "ok  $dir/$base (MIR=1 == MIR=0)"
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
