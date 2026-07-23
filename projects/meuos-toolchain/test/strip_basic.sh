#!/bin/sh
# strip_basic.sh - 验证 strip 工具基本功能
# Usage: strip_basic.sh <strip> <test-obj> <test-exec>
set -eu
STRIP="${1:?usage: strip_basic.sh <strip> <obj> <exec>}"
OBJ="${2:?usage: strip_basic.sh <strip> <obj> <exec>}"
EXEC="${3:?usage: strip_basic.sh <strip> <obj> <exec>}"

export LC_ALL=C

tmp=$(mktemp -d /tmp/mt-strip.XXXXXX)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

# 1. --version
"$STRIP" --version | grep -q '^meuos-toolchain strip 0\.2\.0'

# 2. --help
"$STRIP" --help >/dev/null

# 3. --strip-all on .o: symtab/strtab 被删除
cp "$OBJ" "$tmp/stripped.o"
"$STRIP" --strip-all "$tmp/stripped.o"
# host readelf 验证无 .symtab
readelf -S "$tmp/stripped.o" | grep -q '\.symtab' && {
    echo "FAIL: .symtab still present after --strip-all"; exit 1; }
readelf -S "$tmp/stripped.o" | grep -q '\.text' || {
    echo "FAIL: .text removed"; exit 1; }

# 4. --strip-debug on .o: .symtab 保留
cp "$OBJ" "$tmp/debug.o"
"$STRIP" --strip-debug "$tmp/debug.o"
readelf -S "$tmp/debug.o" | grep -q '\.symtab' || {
    echo "FAIL: .symtab removed by --strip-debug"; exit 1; }

# 5. -o 输出到新文件
"$STRIP" -o "$tmp/out.o" "$OBJ"
test -f "$tmp/out.o"
# 输入未改变
readelf -S "$OBJ" | grep -q '\.symtab'

# 6. 可执行文件 strip 后仍可运行
cp "$EXEC" "$tmp/stripped.exec"
"$STRIP" "$tmp/stripped.exec"
"$tmp/stripped.exec"

# 7. -R 删除指定节区
cp "$OBJ" "$tmp/removed.o"
"$STRIP" -R .comment "$tmp/removed.o"
! readelf -S "$tmp/removed.o" | grep -q '\.comment' || {
    echo "FAIL: .comment still present"; exit 1; }

echo "mt strip basic: PASS"
