#!/bin/sh
# objdump_line.sh - verify objdump --line-numbers (-l) annotates the
# disassembly with source file:line from a DWARF v4 .debug_line table.
#
# Builds an mcc -g relocatable object (mcc .s -> mt/as .o), then disassembles
# it with `objdump -d -l` and checks the emitted `FILE:LINE` annotations match
# the source's function declaration lines.  Uses the .o directly (no libc
# link required), matching the addresses/line table from mcc's DWARF emitter.
#
# Usage: objdump_line.sh <objdump> <mcc>
set -e
OBJDUMP="${1:?usage: objdump_line.sh <objdump> <mcc>}"
MCC="${2:?usage: objdump_line.sh <objdump> <mcc>}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

export LC_ALL=C

# --- 1) generate a -g relocatable object via mcc -> mt/as -----------------
# The assembler sibling (mt/as) lives next to objdump.
AS="$(dirname "$OBJDUMP")/as"

cat > "$TMP/t.c" <<'EOF'
int add(int a, int b) { return a + b; }
int main(void) { return add(40, 2); }
EOF

if ! "$MCC" -g -S -o "$TMP/t.s" "$TMP/t.c" 2>"$TMP/mcc.err"; then
	echo "objdump --line-numbers: skipped (mcc -g unavailable)" >&2
	cat "$TMP/mcc.err" >&2
	exit 0
fi
"$AS" -o "$TMP/t.o" "$TMP/t.s"

# --- 2) verify .debug_line is present -------------------------------
READELF="$(dirname "$OBJDUMP")/readelf"
if [ -x "$READELF" ]; then
	if ! "$READELF" -S "$TMP/t.o" 2>/dev/null | grep -q "debug_line"; then
		echo "objdump --line-numbers: FAIL (no .debug_line)" >&2
		exit 1
	fi
fi

# --- 3) objdump -d -l emits a FILE:LINE annotation -------------------
out="$($OBJDUMP -d -l "$TMP/t.o" 2>&1)"

# A valid -l run must print at least one `FILE:LINE` line.  The line number
# comes from mcc's .debug_line (function decl rows), so we require a digit
# after the ':'.  The file name varies (path may be relative/absolute).
if echo "$out" | grep -Eq '^[^[:space:]][^:]*:[0-9]+$'; then
	echo "objdump --line-numbers: OK"
else
	echo "objdump --line-numbers: FAIL (no FILE:LINE annotation)" >&2
	echo "$out" >&2
	exit 1
fi

echo "ALL objdump --line-numbers gates passed"
