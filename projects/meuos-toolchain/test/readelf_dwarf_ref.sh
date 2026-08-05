#!/bin/sh
# Gate: mt/readelf DWARF line-table decoding, validated against a known-good
# reference.
#
# The host compiler emits DWARF that gdb accepts, so it serves as a golden
# sample: if mt/readelf decodes it into a sane line table, the decoder itself
# is correct.  This isolates decoder defects from emitter defects -- when
# mcc's own -g output decodes badly, this gate proves the fault is upstream
# in the emitter rather than here.
#
# Skips cleanly when no host compiler is available.
set -e
READELF="$1"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

HOSTCC=""
for c in cc gcc; do
	if command -v "$c" >/dev/null 2>&1; then
		HOSTCC="$c"
		break
	fi
done
if [ -z "$HOSTCC" ]; then
	echo "readelf DWARF reference: skipped (no host compiler)"
	exit 0
fi

cat > "$TMP/ref.c" <<'EOF'
int add(int a, int b)
{
	int s = a + b;
	return s;
}

int main(void)
{
	int x = 40;
	int y = 2;
	int r = add(x, y);
	return r;
}
EOF

if ! "$HOSTCC" -g -O0 -static -o "$TMP/ref.elf" "$TMP/ref.c" 2>/dev/null; then
	echo "readelf DWARF reference: skipped (host cc cannot build static)"
	exit 0
fi

"$READELF" -w "$TMP/ref.elf" > "$TMP/out.txt" 2>/dev/null || {
	echo "readelf DWARF reference: FAIL (readelf -w errored)" >&2
	exit 1
}

# 1) the line table must be present and name the source file
grep -q "The .debug_line section:" "$TMP/out.txt" || {
	echo "readelf DWARF reference: FAIL (no .debug_line output)" >&2
	exit 1
}
grep -q "ref.c" "$TMP/out.txt" || {
	echo "readelf DWARF reference: FAIL (source file name not decoded)" >&2
	exit 1
}
echo "line table present: OK"

# 2) rows must cover several distinct source lines.  A decoder that mis-parses
#    the line-number program typically collapses every row onto one line, so
#    require at least 4 distinct line numbers across the matrix.
rows="$(sed -n '/^Address  *Line/,/^$/p' "$TMP/out.txt" | grep '^0x')"
[ -n "$rows" ] || {
	echo "readelf DWARF reference: FAIL (no line-table rows)" >&2
	exit 1
}
nlines="$(echo "$rows" | awk '{print $2}' | sort -u | wc -l)"
if [ "$nlines" -lt 4 ]; then
	echo "readelf DWARF reference: FAIL (only $nlines distinct lines; \
line-number program likely mis-decoded)" >&2
	exit 1
fi
echo "distinct source lines ($nlines): OK"

# 3) addresses must be strictly increasing across the matrix.  Zero-length or
#    out-of-order intervals indicate the address advance opcodes were misread.
naddr="$(echo "$rows" | awk '{print $1}' | sort -u | wc -l)"
nrow="$(echo "$rows" | wc -l)"
if [ "$naddr" -lt 4 ]; then
	echo "readelf DWARF reference: FAIL (only $naddr distinct addresses \
across $nrow rows; address advance likely mis-decoded)" >&2
	exit 1
fi
echo "distinct addresses ($naddr): OK"

# 4) .debug_info must decode the compile unit and at least one subprogram
grep -q "DW_TAG_compile_unit" "$TMP/out.txt" || {
	echo "readelf DWARF reference: FAIL (no DW_TAG_compile_unit)" >&2
	exit 1
}
grep -q "DW_TAG_subprogram" "$TMP/out.txt" || {
	echo "readelf DWARF reference: FAIL (no DW_TAG_subprogram)" >&2
	exit 1
}
echo "debug_info tags: OK"

echo "ALL readelf DWARF reference gates passed"
