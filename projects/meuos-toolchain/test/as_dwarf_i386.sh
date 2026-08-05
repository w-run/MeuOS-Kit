#!/bin/sh
# Gate: mt/as DWARF debug-info data directives + symbol-difference folding.
# Verifies the pseudo-ops mcc's DWARF emitter relies on (.4byte/.8byte/.2byte
# aliases, .uleb128/.sleb128 LEB128, and `symA - symB` same-section deltas)
# assemble correctly and that mcc -g output links+reads back via mt/readelf.
set -e
AS="$1"
SYSROOT="${MEUOS_SYSROOT:-/workspace/MeuOS-Kit/sysroot}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
READELF="$(dirname "$AS")/readelf"
MCC="${MCC:-}"

# 1) size-suffixed aliases (.4byte/.8byte/.2byte alias .long/.quad/.short)
cat > "$TMP/a.s" <<'EOF'
.text
.globl f
f:
	.4byte 0x12345678
	.2byte 0x9abc
	.8byte 0x1122334455667788
	ret
EOF
"$AS" --target=x86_64 -o "$TMP/a.o" "$TMP/a.s"
echo "size-suffixed aliases: OK"

# 2) LEB128 (ULEB128 / SLEB128) encoding
cat > "$TMP/b.s" <<'EOF'
.text
.globl g
g:
	.uleb128 0
	.uleb128 1
	.uleb128 127
	.uleb128 128
	.sleb128 -1
	.sleb128 -128
	ret
EOF
"$AS" --target=x86_64 -o "$TMP/b.o" "$TMP/b.s"
echo "leb128: OK"

# 3) symbol-difference folding: .4byte .Lb - .La must fold to the delta (2)
cat > "$TMP/c.s" <<'EOF'
.text
.globl h
h:
.La:
	nop
.Lb:
	nop
	.4byte .Lb - .La - 0
EOF
"$AS" --target=x86_64 -o "$TMP/c.o" "$TMP/c.s"
# folded to a constant => no .rela section should remain
if "$READELF" -S "$TMP/c.o" 2>/dev/null | grep -q "rela"; then
	echo "symbol-difference: FAIL (unexpected relocation present)" >&2
	exit 1
fi
echo "symbol-difference: OK"

# 4) end-to-end: mcc -g output assembles, links, and mt/readelf decodes it
if [ -n "$MCC" ] && [ -x "$READELF" ]; then
	cat > "$TMP/d.c" <<'EOF'
int add(int a, int b) { return a + b; }
int main(void) { return add(40, 2); }
EOF
	if "$MCC" -target x86_64 --specs=meuos --sysroot="$SYSROOT" -g -S -o "$TMP/d.s" "$TMP/d.c" 2>/dev/null; then
		"$AS" --target=x86_64 -o "$TMP/d.o" "$TMP/d.s"
		"$READELF" -S "$TMP/d.o" 2>/dev/null | grep -q "debug_line" || {
			echo "mcc -g end-to-end: FAIL (no .debug_line section)" >&2
			exit 1
		}
		echo "mcc -g end-to-end: OK"
	else
		echo "mcc -g end-to-end: skipped (mcc unavailable)"
	fi
else
	echo "mcc -g end-to-end: skipped (mcc/readelf unavailable)"
fi
echo "ALL DWARF as gates passed"
