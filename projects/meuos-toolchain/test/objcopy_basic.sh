#!/bin/sh
# objcopy_basic.sh - 验证 objcopy 工具基本功能
# Usage: objcopy_basic.sh <objcopy> <test-obj> <test-exec>
set -eu
OBJCOPY="${1:?usage: objcopy_basic.sh <objcopy> <obj> <exec>}"
OBJ="${2:?usage: objcopy_basic.sh <objcopy> <obj> <exec>}"
EXEC="${3:?usage: objcopy_basic.sh <objcopy> <obj> <exec>}"

export LC_ALL=C

tmp=$(mktemp -d /tmp/mt-objcopy.XXXXXX)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

# 1. --version
"$OBJCOPY" --version | grep -q '^meuos-toolchain objcopy 0\.2\.0'

# 2. --help
"$OBJCOPY" --help >/dev/null

# 3. 默认复制 (节区不变)
"$OBJCOPY" "$OBJ" "$tmp/copy.o"
readelf -S "$OBJ" > "$tmp/orig.secs"
readelf -S "$tmp/copy.o" > "$tmp/copy.secs"
diff "$tmp/orig.secs" "$tmp/copy.secs" >/dev/null || {
    echo "FAIL: default copy changed sections"; exit 1; }

# 4. -S strip-all
"$OBJCOPY" -S "$OBJ" "$tmp/stripped.o"
! readelf -S "$tmp/stripped.o" | grep -q '\.symtab' || {
    echo "FAIL: -S did not strip symtab"; exit 1; }

# 5. -R 移除节区 (用 .comment, 若无则跳过)
if readelf -S "$OBJ" | grep -q '\.comment'; then
    "$OBJCOPY" -R .comment "$OBJ" "$tmp/removed.o"
    ! readelf -S "$tmp/removed.o" | grep -q '\.comment' || {
        echo "FAIL: -R did not remove section"; exit 1; }
fi

# 6. --add-section 添加节区
echo "hello" > "$tmp/payload.bin"
"$OBJCOPY" --add-section .custom="$tmp/payload.bin" "$OBJ" "$tmp/added.o"
readelf -S "$tmp/added.o" | grep -q '\.custom' || {
    echo "FAIL: --add-section did not add section"; exit 1; }

# 7. -j only-section
"$OBJCOPY" -j .text "$OBJ" "$tmp/only.o"
readelf -S "$tmp/only.o" | grep -q '\.text'
readelf -S "$tmp/only.o" | grep -q '\.symtab' && {
    echo "FAIL: -j kept .symtab"; exit 1; }

# 8. --dump-section 导出节区内容
"$OBJCOPY" --dump-section .text="$tmp/dump.text" "$OBJ" "$tmp/dummy.o"
test -s "$tmp/dump.text"

# 9. 可执行文件 strip 后仍可运行
"$OBJCOPY" -S "$EXEC" "$tmp/stripped.exec"
"$tmp/stripped.exec"

# 10. -O binary：dump 出可加载字节
"$OBJCOPY" -O binary "$EXEC" "$tmp/out.bin"
test -s "$tmp/out.bin"

# 11. -O ihex：Intel HEX（data 记录 + EOF，每行校验和合法）
"$OBJCOPY" -O ihex "$EXEC" "$tmp/out.hex"
grep -Eq '^:[0-9A-Fa-f]{10,}$' "$tmp/out.hex"
grep -Eq ':00000001FF$' "$tmp/out.hex"
awk '
  /^:[0-9A-Fa-f]+$/ {
    s=substr($0,2);
    n=length(s);
    if (n % 2) { print "FAIL bad hex length: " $0; exit 1 }
    sum=0;
    for (i=1; i<=n; i+=2) {
      byte=("0x" substr(s,i,2)) + 0;
      sum=(sum+byte)%256;
    }
    if (sum != 0) { print "FAIL bad ihex checksum: " $0; exit 1 }
    seen=1
  }
  END { if (!seen) { print "FAIL no ihex records"; exit 1 } }
' "$tmp/out.hex" || { echo "FAIL: -O ihex output invalid"; exit 1; }

# 12. -O srec：Motorola S-record（数据记录 + 终止记录）
"$OBJCOPY" -O srec "$EXEC" "$tmp/out.srec"
grep -Eq '^S[0-9A-F][0-9A-Fa-f]+$' "$tmp/out.srec"
grep -Eq '^S[789][0-9A-Fa-f]+$' "$tmp/out.srec" || {
    echo "FAIL: -O srec missing termination record"; exit 1; }

# 13. 非法格式拒绝
if "$OBJCOPY" -O bogus "$OBJ" "$tmp/x" >/dev/null 2>&1; then
    echo "FAIL: -O bogus should fail"; exit 1; fi

echo "mt objcopy basic: PASS"
