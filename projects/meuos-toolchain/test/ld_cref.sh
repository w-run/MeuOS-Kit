#!/bin/sh
set -eu

ld=${1:?ld path required}
as=${2:?as path required}
work=$(mktemp -d /tmp/mt-ld-cref.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM

# Single-file test (multi-file requires relocation grouping that may discard sections)
cat >"$work/test.s" <<'ASM'
.text
.globl func_a
func_a:
    mov $0, %eax
    ret
.globl func_b
func_b:
    mov $1, %eax
    ret
.globl _start
_start:
    call func_a
    mov $0, %eax
    ret
ASM

"$as" -o "$work/test.o" "$work/test.s"

# Link with --cref, capture stderr
output=$("$ld" -o "$work/out" --cref "$work/test.o" 2>&1 >/dev/null) || true

# Verify output contains expected symbols
echo "$output" | grep -q "func_a" || { echo "FAIL: func_a not in cref"; exit 1; }
echo "$output" | grep -q "func_b" || { echo "FAIL: func_b not in cref"; exit 1; }
echo "$output" | grep -q "_start" || { echo "FAIL: _start not in cref"; exit 1; }
echo "$output" | grep -q "Cross-reference" || { echo "FAIL: header not found"; exit 1; }

# Verify value is non-zero hex
echo "$output" | grep -q "0x0040" || { echo "FAIL: value not hex"; exit 1; }

echo "mt/ld --cref: all checks PASS"
