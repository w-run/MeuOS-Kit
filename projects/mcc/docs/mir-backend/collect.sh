#!/bin/bash
# P0 基线采集脚本 — 冻结当前 bridge 路径（MIR→LIR）为 oracle。
#
# 作用：用当前 mcc/m++（MIR 管线默认开启）编译测试集，保存每个测试的
#       .s 输出、编译命令与退出码，供 P1-P3 移植后的 asm-diff 对照。
#
# 用法：
#   bash collect.sh [MCC 路径] [M++ 路径] [C++ 测试目录] [输出目录]
#   默认：bash collect.sh ./mcc ./m++ /tmp/mxx-t /tmp/mir-backend-base
#
# 收集范围：
#   1. test/c99|test/c11|test/abi 下所有 .c（C 前端，--specs=host -S）
#   2. <C++测试目录>/*.cc（C++ 前端 m++，--specs=host -S）
#   3. 代表性程序 hello/fib/struct/aggregate（生成源码，-S + 汇编运行验证）
#
# include 策略：
#   - C 测试统一加 -I<meuos-libc>/include（mcc 目标 libc 的标准头：
#     stdio/stdint/stddef/stdarg 等，host glibc 头无法被 mcc 解析）
#   - c11 测试另加 -Itest/c11（Makefile check-c11 惯例，stdatomic shim 优先）
#
# 负向测试：源码含 "should fail"/"应报错"/"must not" 标记的，编译失败为
#   预期行为（诊断测试），计入 expected-fail，不计入意外失败。
#
# 输出布局：
#   BASE/
#     collect.sh            （本脚本副本）
#     asm/<name>.s          （每个测试的汇编输出）
#     meta/<name>.cmd       （编译命令，可直接重跑）
#     meta/<name>.status    （OK/FAIL/EXPECTED + 退出码）
#     meta/<name>.err       （stderr）
#     meta/summary.txt      （汇总：总数/成功/失败/expected + 环境）

set -u

MCC="${1:-./mcc}"
MPP="${2:-./m++}"
CXXT="${3:-/tmp/mxx-t}"
BASE="${4:-/tmp/mir-backend-base}"

# 绝对化编译器路径，保证 repro 可在任意 cwd 重跑
MCC="$(readlink -f "$MCC")"
MPP="$(readlink -f "$MPP")"

# 由 mcc 可执行文件位置推断 mcc 源码树（test/ 与其下）
SRCTREE="$(dirname "$MCC")"
TESTDIR="$SRCTREE/test"
LIBINC="$SRCTREE/../meuos-libc/include"   # mcc 目标 libc 标准头

mkdir -p "$BASE/asm" "$BASE/meta"
: > "$BASE/meta/summary.txt"

log() { printf '%s\n' "$*" | tee -a "$BASE/meta/summary.txt"; }

log "=== P0 MIR-backend oracle baseline ==="
log "date:      $(date -u +%Y-%m-%dT%H:%M:%SZ)"
log "mcc:       $MCC"
log "m++:       $MPP"
log "src tree:  $SRCTREE"
log "libc incl: $LIBINC"
log "mcc ver:   $($MCC --version 2>&1 | head -1)"
log "mpp ver:   $($MPP --version 2>&1 | head -1)"
log ""

TOTAL=0; OK=0; FAIL=0; EXPECTED=0

# 规范化测试名
norm() {
	local dir="$1" file="$2"
	printf '%s_%s' "$(basename "$dir")" "$(basename "$file" .cc | sed 's/\.[cC]$//')"
}

# 负向测试判定（编译失败是预期）
is_expected_fail() {
	grep -qE 'should fail|应报错|must not' "$1"
}

# 编译一个测试并记录 .s / 命令 / 退出码。参数: <规范名> <编译器> <源> <额外flag...>
emit() {
	local name="$1" cc="$2" src="$3"; shift 3
	TOTAL=$((TOTAL+1))
	local asm="$BASE/asm/$name.s"
	local cmdline="$cc -S --specs=host -o $asm $* $src"
	printf '%s\n' "$cmdline" > "$BASE/meta/$name.cmd"
	if $cc -S --specs=host -o "$asm" "$@" "$src" 2> "$BASE/meta/$name.err"; then
		OK=$((OK+1)); echo "OK    exit=0" > "$BASE/meta/$name.status"
		cat > /dev/null <<'EOF'
EOF
	else
		if is_expected_fail "$src"; then
			EXPECTED=$((EXPECTED+1))
			echo "EXPECTED exit=0 (negative test)" > "$BASE/meta/$name.status"
		else
			FAIL=$((FAIL+1)); echo "FAIL  exit=$?" > "$BASE/meta/$name.status"
		fi
	fi
	{ echo "=== $name ==="; printf 'cmd: %s\n' "$cmdline"; \
	  echo "status: $(cat "$BASE/meta/$name.status")"; \
	  [ -s "$BASE/meta/$name.err" ] && { echo "stderr:"; head -4 "$BASE/meta/$name.err"; } \
	} >> "$BASE/meta/summary.txt"
}

shopt -s nullglob

# ---- 1. C 测试（c99 / c11 / abi）----
log "--- C tests (c99/c11/abi) ---"
for dir in "$TESTDIR"/c99 "$TESTDIR"/c11 "$TESTDIR"/abi; do
	[ -d "$dir" ] || continue
	for src in "$dir"/*.c; do
		case "$dir" in
		*c11) emit "$(norm "$dir" "$src")" "$MCC" "$src" -I"$TESTDIR/c11" -I"$LIBINC" ;;
		*)    emit "$(norm "$dir" "$src")" "$MCC" "$src" -I"$LIBINC" ;;
		esac
	done
done

# ---- 2. C++ 测试（m++，--specs=host）----
log "--- C++ tests (m++ / $CXXT) ---"
if [ -d "$CXXT" ]; then
	for src in "$CXXT"/*.cc; do
		emit "cpp_$(basename "$src" .cc)" "$MPP" "$src" -I"$LIBINC"
	done
else
	log "CXXT=$CXXT not found, skipping"
fi

# ---- 3. 代表性程序（生成源码，-S + 汇编运行验证）----
log "--- representative programs ---"
GEN="$BASE/gen"
mkdir -p "$GEN"
cat > "$GEN/hello.c" <<'EOF'
#include <stdio.h>
int main(void){ printf("hello from mcc\n"); return 0; }
EOF
cat > "$GEN/fib.c" <<'EOF'
#include <stdio.h>
int fib(int n){ return n<2 ? n : fib(n-1)+fib(n-2); }
int main(void){ printf("fib(10)=%d\n", fib(10)); return fib(10)==55 ? 0 : 1; }
EOF
cat > "$GEN/struct.c" <<'EOF'
#include <stdio.h>
struct S { int a; long b; char c; };
static struct S mk(int a, long b, char c){ struct S s = {a,b,c}; return s; }
static long sum(struct S s){ return s.a + s.b + s.c; }
int main(void){ struct S s = mk(3, 4L, 5); return sum(s)==12 ? 0 : 1; }
EOF
cat > "$GEN/aggregate.c" <<'EOF'
#include <stdio.h>
typedef struct { int a[3]; double d; } Big; /* 超过 16 字节 -> sret */
static Big mk(void){ Big b = {{1,2,3}, 4.0}; return b; }
static int chk(Big b){ return b.a[0]+b.a[1]+b.a[2]==6 && b.d==4.0 ? 0 : 1; }
int main(void){ return chk(mk()); }
EOF
for f in hello fib struct aggregate; do
	emit "rep_$f" "$MCC" "$GEN/$f.c" -I"$LIBINC"
	if [ -s "$BASE/asm/rep_$f.s" ]; then
		if gcc -c -o "$GEN/$f.o" "$BASE/asm/rep_$f.s" 2>/dev/null &&
		   gcc -o "$GEN/$f.bin" "$GEN/$f.o" 2>/dev/null; then
			"$GEN/$f.bin" > /dev/null 2>&1
			rc=$?
			echo "run rc=$rc" >> "$BASE/meta/rep_$f.status"
			log "rep_$f: asm linked, run rc=$rc"
		else
			log "rep_$f: asm failed to link with host gcc"
		fi
	fi
done

# ---- 4. 可重复性校验：抽前 6 个成功测试，按 .cmd 重跑并 diff ----
log "--- reproducibility check ---"
CHK=0; DUP=0
for st in "$BASE"/meta/*.status; do
	[ "$CHK" -ge 6 ] && break
	name="$(basename "$st" .status)"
	grep -q '^OK' "$st" || continue
	cmd="$(cat "$BASE/meta/$name.cmd")"
	# 重跑时把 -o 输出路径替换为 rechk
	recmd="$(printf '%s\n' "$cmd" | sed "s|-o [^ ]*|-o $BASE/meta/rechk_$name.s|")"
	if bash -c "$recmd" 2>/dev/null; then
		if cmp -s "$BASE/asm/$name.s" "$BASE/meta/rechk_$name.s"; then
			CHK=$((CHK+1)); log "reproducible: $name"
		else
			DUP=$((DUP+1)); log "DIFFERS:      $name"
		fi
	else
		DUP=$((DUP+1)); log "RERUN-FAIL:   $name"
	fi
done
log "repro: $CHK identical, $DUP differ/fail"
log ""

log "=== summary ==="
log "total: $TOTAL, ok: $OK, expected-fail: $EXPECTED, unexpected-fail: $FAIL"
log "asm files: $(ls "$BASE/asm"/*.s 2>/dev/null | wc -l)"
