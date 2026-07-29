#!/bin/sh
set -eu

as=${1:?as path required}
work=$(mktemp -d /tmp/mt-as-equ.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM

fail=0

# Test 1: .equ basic usage
cat >"$work/equ1.s" <<'ASM'
.text
.equ CONST, 42
.globl _start
_start:
    movl $0, %eax
    ret
ASM
"$as" -o "$work/equ1.o" "$work/equ1.s" 2>/dev/null || {
    echo "FAIL: .equ basic assembly failed"; fail=1
}

# Verify the symbol exists in output with ABS section and correct value
readelf -s "$work/equ1.o" 2>/dev/null | grep -q "CONST" || {
    echo "FAIL: CONST symbol not found in .o"; fail=1
}
readelf -s "$work/equ1.o" 2>/dev/null | grep "CONST" | grep -q "ABS" || {
    echo "FAIL: CONST not marked as ABS"; fail=1
}
readelf -s "$work/equ1.o" 2>/dev/null | grep "CONST" | grep -q "002a" || {
    echo "FAIL: CONST value not 0x2a (42)"; fail=1
}

# Test 2: .set as alias for .equ
cat >"$work/equ2.s" <<'ASM'
.text
.set ANSWER, 42
.globl _start
_start:
    movl $0, %eax
    ret
ASM
"$as" -o "$work/equ2.o" "$work/equ2.s" 2>/dev/null || {
    echo "FAIL: .set basic assembly failed"; fail=1
}

readelf -s "$work/equ2.o" 2>/dev/null | grep -q "ANSWER" || {
    echo "FAIL: ANSWER symbol not found in .o"; fail=1
}

# Test 3: .equ with hex value
cat >"$work/equ3.s" <<'ASM'
.text
.equ SYS_EXIT, 0x3c
.globl _start
_start:
    movl $0, %eax
    ret
ASM
"$as" -o "$work/equ3.o" "$work/equ3.s" 2>/dev/null || {
    echo "FAIL: .equ hex value assembly failed"; fail=1
}

# Test 4: .warning should not cause failure
cat >"$work/warn.s" <<'ASM'
.text
.warning "this is a test warning"
.globl _start
_start:
    ret
ASM
output=$("$as" -o "$work/warn.o" "$work/warn.s" 2>&1) || {
    echo "FAIL: .warning should not cause failure"; fail=1
}
echo "$output" | grep -q "warning:" || {
    echo "FAIL: .warning output missing"; fail=1
}

# Test 5: .error should cause failure
cat >"$work/err.s" <<'ASM'
.text
.error "this should fail"
ASM
if "$as" -o "$work/err.o" "$work/err.s" 2>/dev/null; then
    echo "FAIL: .error should have failed assembly"; fail=1
fi

# Test 6: .abort should cause failure
cat >"$work/abort.s" <<'ASM'
.text
.abort
ASM
if "$as" -o "$work/abort.o" "$work/abort.s" 2>/dev/null; then
    echo "FAIL: .abort should have failed assembly"; fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "mt/as .equ/.set/.error/.warning: FAILED"
    exit 1
fi
echo "mt/as .equ/.set/.error/.warning: all checks PASS"
