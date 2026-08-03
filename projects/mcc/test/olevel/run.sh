#!/bin/sh
# check-olevel — mcc -O 优化级别语义分级回归。
#
# 验证（对照 src/mir/passes.c 的 run_mir_passes 分级）：
#   1) 运行时正确性：-O0/-O1/-O2/-O3/-Os/-Oz/-Og 编译运行结果一致（exit 0）
#   2) 级别差异实际生效：
#      -O0 汇编指令数 > -O1（-O0 禁用优化）
#      -O1 保留分支、-O2 if 转换出 cmov
#      -O2 出 imul、-O3/-Os/-Oz 出 shl（mul 2^n 强度削减）
#      -Og 叶函数保留 pushq %rbp（帧指针），-O2 省略
#   3) 非法级别：-O9 钳制到 -O3（警告不静默），-Ox 报错
#
# 用法：sh test/olevel/run.sh [mcc 二进制]
set -e
BIN=${1:-./mcc}
DIR=$(dirname "$0")

fail() { echo "FAIL: $*" >&2; exit 1; }

# --- 1) 运行时正确性：所有级别必须编译且运行返回 0 ---
for lvl in 0 1 2 3 s z g; do
	$BIN -O$lvl --specs=host -o /tmp/olevel-grading-$lvl "$DIR/grading.c" 2>/dev/null \
		|| fail "-O$lvl compile failed"
	/tmp/olevel-grading-$lvl || fail "-O$lvl runtime wrong (grading.c)"
done

# --- 2) 级别差异 ---
# -O0 指令数 > -O1（-O0 禁用优化）
$BIN -O0 --specs=host -S -o /tmp/olevel-o0.s "$DIR/grading.c" 2>/dev/null
$BIN -O1 --specs=host -S -o /tmp/olevel-o1.s "$DIR/grading.c" 2>/dev/null
n0=$(grep -cE '^\s+[a-z]' /tmp/olevel-o0.s)
n1=$(grep -cE '^\s+[a-z]' /tmp/olevel-o1.s)
[ "$n0" -gt "$n1" ] || fail "-O0 ($n0) should have more asm instructions than -O1 ($n1)"

# -O1 分支 vs -O2 cmov（if 转换）
$BIN -O1 --specs=host -S -o /tmp/olevel-b1.s "$DIR/branch.c" 2>/dev/null
$BIN -O2 --specs=host -S -o /tmp/olevel-b2.s "$DIR/branch.c" 2>/dev/null
grep -q cmov /tmp/olevel-b2.s || fail "-O2 should if-convert to cmov"
if grep -q cmov /tmp/olevel-b1.s; then fail "-O1 should keep a branch (no cmov)"; fi

# -O2 imul，-O3/-Os/-Oz shl（mul 2^n 强度削减）
$BIN -O2 --specs=host -S -o /tmp/olevel-m2.s "$DIR/mul8.c" 2>/dev/null
grep -q imul /tmp/olevel-m2.s || fail "-O2 should emit imul for x*8"
for lvl in 3 s z; do
	$BIN -O$lvl --specs=host -S -o /tmp/olevel-m$lvl.s "$DIR/mul8.c" 2>/dev/null
	grep -q shl /tmp/olevel-m$lvl.s || fail "-O$lvl should strength-reduce x*8 to shl"
	if grep -q imul /tmp/olevel-m$lvl.s; then fail "-O$lvl should not emit imul (x*8 -> shl)"; fi
done

# -Og 保留叶函数帧指针，-O2 省略（leaf.c 只含叶函数，无 main 噪声）
$BIN -Og --specs=host -S -o /tmp/olevel-og.s "$DIR/leaf.c" 2>/dev/null
$BIN -O2 --specs=host -S -o /tmp/olevel-o2leaf.s "$DIR/leaf.c" 2>/dev/null
grep -q 'pushq %rbp' /tmp/olevel-og.s || fail "-Og should keep frame pointer for leaf function"
if grep -q 'pushq %rbp' /tmp/olevel-o2leaf.s; then fail "-O2 leaf should omit frame pointer"; fi

# --- 3) 非法级别 ---
if $BIN -O9 --specs=host -c -o /tmp/olevel-o9.o "$DIR/grading.c" 2>/tmp/olevel-o9.log; then
	grep -q "clamping to -O3" /tmp/olevel-o9.log || fail "-O9 should print clamping warning"
else
	fail "-O9 should clamp to -O3 (not fail)"
fi
if $BIN -Ox --specs=host -c -o /tmp/olevel-ox.o "$DIR/grading.c" >/dev/null 2>&1; then
	fail "-Ox should be rejected (unknown optimization level)"
fi

echo "PASS: check-olevel (-O0..-O3/-Os/-Oz/-Og) 运行时正确性 + 级别差异 + 非法级别"
