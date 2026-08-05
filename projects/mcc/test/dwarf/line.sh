#!/bin/sh
# DWARF .debug_line regression gate.
#
# mcc emits the line-number program itself (emit/dwarf.c).  A historical bug
# encoded DW_LNS_advance_line as a bare .sleb128 with no opcode byte, so the
# line reader parsed the delta (e.g. 0) as an extended-opcode prefix and
# desynchronised the whole program -> "No line number information available".
# Another bug hard-wired the DW_LNE_set_address length to 9, which is wrong
# for 32-bit targets.
#
# This gate compiles with -g, assembles with the system 'as' + 'ld' (a
# controlled toolchain, independent of mt/as), and asks gdb for the line of
# each function.  gdb MUST report a real "Line N of ..." for every function;
# if the line program is malformed, gdb reports "No line number information".
#
# Usage: line.sh [mcc-binary]

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mcc=${1:-"$root/mcc"}

for as in as; do :; done
if ! command -v as >/dev/null 2>&1 || ! command -v ld >/dev/null 2>&1 \
   || ! command -v gdb >/dev/null 2>&1; then
	printf '%s\n' 'dwarf line gate: as/ld/gdb not available, skipping'
	exit 0
fi

work=${TMPDIR:-/tmp}/mcc-dwarf-line.$$
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work"

"$mcc" -g -S -o "$work/line.s" "$root/test/dwarf/line.c"

as -o "$work/line.o" "$work/line.s"
ld -o "$work/line.elf" "$work/line.o" -e main

fail=0
for fn in add sub main; do
	if gdb -batch -ex "info line $fn" "$work/line.elf" 2>/dev/null \
	   | grep -q "Line [0-9]* of"; then
		printf '%s\n' "dwarf line: $fn -> OK"
	else
		printf '%s\n' "dwarf line: $fn -> FAIL (no line info)" >&2
		fail=1
	fi
done

if [ "$fail" -ne 0 ]; then
	printf '%s\n' 'dwarf line gate FAILED' >&2
	exit 1
fi
printf '%s\n' 'dwarf .debug_line regression gate passed'
