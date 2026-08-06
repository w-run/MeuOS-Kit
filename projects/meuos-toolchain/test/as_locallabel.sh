#!/bin/sh
# as_locallabel.sh - numeric local label (Nf/Nb) regression gate.
set -eu

as=${1:?mt/as path required}
ld=${2:-}

work=$(mktemp -d /tmp/mt-as-locallabel.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM
fail=0

# --- Test 1: forward reference (1f) ---
cat >"$work/fwd.s" <<'ASM'
.text
.globl _start
_start:
	jmp 1f
	mov $1, %rdi
	mov $60, %eax
	syscall
1:
	mov $42, %rdi
1:
	mov $60, %eax
	syscall
ASM

"$as" --target=x86_64 -o "$work/fwd.o" "$work/fwd.s" 2>"$work/fwd.err" || {
	echo "FAIL (fwd): assemble error: $(cat $work/fwd.err)"
	fail=1
}
if [ -n "$ld" ]; then
	"$ld" -static -o "$work/fwd.elf" "$work/fwd.o" 2>/dev/null || true
	if [ -f "$work/fwd.elf" ]; then
		set +e; "$work/fwd.elf"; rc=$?; set -e
		[ "$rc" = 42 ] || { echo "FAIL (fwd): exit=$rc (expected 42)"; fail=1; }
	fi
fi

# --- Test 2: backward reference (1b) ---
cat >"$work/bwd.s" <<'ASM'
.text
.globl _start
_start:
	jmp 2f
1:	mov $1, %rdi
	mov $60, %eax
	syscall
1:	mov $42, %rdi
	mov $60, %eax
	syscall
2:	jmp 1b
ASM

"$as" --target=x86_64 -o "$work/bwd.o" "$work/bwd.s" 2>"$work/bwd.err" || {
	echo "FAIL (bwd): assemble error: $(cat $work/bwd.err)"
	fail=1
}
if [ -n "$ld" ]; then
	"$ld" -static -o "$work/bwd.elf" "$work/bwd.o" 2>/dev/null || true
	if [ -f "$work/bwd.elf" ]; then
		set +e; "$work/bwd.elf"; rc=$?; set -e
		[ "$rc" = 42 ] || { echo "FAIL (bwd): exit=$rc (expected 42)"; fail=1; }
	fi
fi

# --- Test 3: multi-def same-num labels ---
cat >"$work/multi.s" <<'ASM'
.text
.globl _start
_start:
1:	nop
1:	nop
1:	nop
	mov $60, %eax
	xor %rdi, %rdi
	syscall
ASM

"$as" --target=x86_64 -o "$work/multi.o" "$work/multi.s" 2>"$work/multi.err" || {
	echo "FAIL (multi): assemble error: $(cat $work/multi.err)"
	fail=1
}

if [ "$fail" -ne 0 ]; then
	echo "mt/as local-label: FAILED"
	exit 1
fi
echo "mt/as local-label (fwd+bwd+multi): all checks PASS"