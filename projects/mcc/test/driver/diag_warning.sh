#!/bin/sh
# check-diag-warning — mcc 诊断警告回归（p9-ui）。
#
# 验证（对照 src/driver/main.c 的 -W 选项 + src/c/lex/token.c 的 cc_warn）：
#   1) 默认（无 -W）不输出任何警告
#   2) -Wall      启用 -Wunused-variable / -Wuninitialized
#   3) -Wextra    -Wall + -Wunused-parameter / -Wsign-compare
#   4) -Wconversion 单独启用截断警告
#   5) -Wno-xxx   关闭单个警告
#   6) 警告附带行号 + caret 的源码上下文（`|` / `^`）
#   7) --lang=zh 警告正文为中文
#
# 用法：sh test/driver/diag_warning.sh [mcc 二进制]
set -eu
BIN=${1:-./mcc}
DIR=$(dirname "$0")

fail() { echo "FAIL: $*" >&2; exit 1; }
work=${TMPDIR:-/tmp}/mcc-diag-warn.$$
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work"

SRC="$DIR/diag_warning.c"
# 除第 7 项外统一 --lang=en：警告正文与终端 LANG 无关，脚本可移植。
LANGOPT="--lang=en"

# --- 1) 默认：无 -W 选项不输出警告 ---
if "$BIN" $LANGOPT --specs=host -c -o "$work/d.o" "$SRC" 2> "$work/default.err"; then
	:
else
	fail "diag_warning.c should compile cleanly"
fi
if [ -s "$work/default.err" ]; then
	fail "default build must emit no warnings (got: $(cat "$work/default.err"))"
fi

# --- 2) -Wall：unused-variable + uninitialized ---
"$BIN" $LANGOPT --specs=host -Wall -c -o "$work/w.o" "$SRC" 2> "$work/wall.err" || true
grep -q "unused variable 'unused_var'" "$work/wall.err" \
	|| fail "-Wall should warn about unused variable"
grep -q "used uninitialized" "$work/wall.err" \
	|| fail "-Wall should warn about uninitialized use"

# --- 3) -Wextra：unused-parameter + sign-compare（含 -Wall 各项）---
"$BIN" $LANGOPT --specs=host -Wextra -c -o "$work/x.o" "$SRC" 2> "$work/extra.err" || true
grep -q "unused parameter 'unused_param'" "$work/extra.err" \
	|| fail "-Wextra should warn about unused parameter"
grep -q "different signedness" "$work/extra.err" \
	|| fail "-Wextra should warn about signed/unsigned comparison"
grep -q "unused variable 'unused_var'" "$work/extra.err" \
	|| fail "-Wextra implies -Wall (unused variable)"

# --- 4) -Wconversion 单独 ---
"$BIN" $LANGOPT --specs=host -Wconversion -c -o "$work/c.o" "$SRC" 2> "$work/conv.err" || true
grep -q "may truncate value" "$work/conv.err" \
	|| fail "-Wconversion should warn about truncating conversion"
# 未带 -Wall：不应出现 unused 警告（细粒度隔离）
if grep -q "unused variable" "$work/conv.err"; then
	fail "-Wconversion alone must not enable unused-variable"
fi

# --- 5) -Wno-xxx：-Wall 后关闭单个警告 ---
"$BIN" $LANGOPT --specs=host -Wall -Wno-uninitialized -c -o "$work/n.o" "$SRC" \
	2> "$work/no.err" || true
if grep -q "used uninitialized" "$work/no.err"; then
	fail "-Wno-uninitialized should silence the uninitialized warning"
fi
grep -q "unused variable 'unused_var'" "$work/no.err" \
	|| fail "-Wno-uninitialized must not disable other -Wall warnings"

# --- 6) 源码上下文：行号栏 `|` + caret `^` ---
grep -E '^ *[0-9]+ \| ' "$work/wall.err" | grep -q . \
	|| fail "warning output must include line-numbered source context"
grep -E '\^' "$work/wall.err" | grep -q . \
	|| fail "warning output must include a caret marker"

# --- 7) --lang=zh 警告正文为中文 ---
"$BIN" --lang=zh --specs=host -Wall -c -o "$work/z.o" "$SRC" 2> "$work/zh.err" || true
grep -q "未使用的变量 'unused_var'" "$work/zh.err" \
	|| fail "--lang=zh warning body should be Chinese"
grep -q "这里" "$work/zh.err" || fail "--lang=zh caret marker should say 这里"

echo "PASS: check-diag-warning (默认关闭 / -Wall / -Wextra / -Wconversion / -Wno- / 上下文 / 双语)"
