#!/bin/sh
# as_rept.sh — test mt/as .rept/.endr repeat directive.
set -eu

as=${1:?as path required}
work=$(mktemp -d /tmp/meuos-toolchain-as-rept.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM

# Test 1: .rept 3 nop → 3 nops in output
cat >"$work/rept3.s" <<'ASM'
.text
.globl _start
_start:
.rept 3
	nop
.endr
ASM
"$as" -o "$work/rept3.o" "$work/rept3.s" 2>&1
nop_count=$(objdump -d "$work/rept3.o" | grep -c '90.*nop')
[ "$nop_count" -eq 3 ] || { printf '%s\n' "FAIL: expected 3 nops, got $nop_count"; exit 1; }
printf '%s\n' 'mt as .rept: 3 nops PASS'

# Test 2: .rept 0 — should error
cat >"$work/rept0.s" <<'ASM'
.rept 0
	nop
.endr
ASM
if "$as" -o "$work/rept0.o" "$work/rept0.s" 2>/dev/null; then
	printf '%s\n' 'FAIL: .rept 0 should error'; exit 1; fi
printf '%s\n' 'mt as .rept: zero count rejected PASS'

# Test 3: .endr without .rept — should error
cat >"$work/norept.s" <<'ASM'
.endr
ASM
if "$as" -o "$work/norept.o" "$work/norept.s" 2>/dev/null; then
	printf '%s\n' 'FAIL: .endr without .rept should error'; exit 1; fi
printf '%s\n' 'mt as .rept: unmatched .endr rejected PASS'

# Test 4: Nested .rept — should error
cat >"$work/nested.s" <<'ASM'
.rept 2
	.rept 3
	nop
	.endr
.endr
ASM
if "$as" -o "$work/nested.o" "$work/nested.s" 2>/dev/null; then
	printf '%s\n' 'FAIL: nested .rept should error'; exit 1; fi
printf '%s\n' 'mt as .rept: nested .rept rejected PASS'

printf '%s\n' 'mt as .rept: all checks PASS'
